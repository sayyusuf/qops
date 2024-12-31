#include <pthread.h> 
#include <unistd.h> 
#include <stdlib.h>
#include <stddef.h>

#include <qops.h>

void
qnode_destroy(struct qnode *node)
{
	if (node->cleanup)
		node->cleanup(node->data);
	if (node->cleanup_node)
		node->cleanup_node(node);
}

int
qnode_exec(struct qnode *node)
{
	int	ret;

	ret = 0;
	if (node->func)
	{
		 ret = node->func(node->data);
		 if (node->err && ret < 0)
			 node->err(node->data, ret);
	}
	qnode_destroy(node);
	return  (ret);
}


struct qnode	*
qnode_new(void *data, int (*func)(void *), void (*cleanup)(void *), void (*err)(void *, int))
{
	struct qnode	*node;

	node = malloc(sizeof (*node));
	if (!node)
		return (NULL);
	*node = (struct qnode){.next = NULL, .cleanup_node = free, .func = func, .cleanup = cleanup, .err = err, .data = data};
	return  (node);
}




void
threadsafeq_append(struct threadsafeq *q, struct qnode *new)
{
	if (!q || !new)
		return ;
	pthread_mutex_lock(&q->lock);
	if (!q->head)
	{
		q->head = new;
		q->tail = new;
		q->n = 1;
	}
	else
	{
		q->tail->next = new;
		q->tail = new;
		++q->n;
	}
	pthread_mutex_unlock(&q->lock);
	if (q->signal)
		q->signal(q->signal_data);
}

struct qnode	*
threadsafeq_remove(struct threadsafeq *q) 
{
	struct qnode	*ret;

	if (!q)
		return (NULL);
	pthread_mutex_lock(&q->lock);
	if (!q->head)
		ret = NULL;
	else
	{
		ret = q->head;
		q->head = ret->next;
		ret->next = NULL;
		--q->n;
	}
	pthread_mutex_unlock(&q->lock);
	return (ret);
}

size_t
threadsafeq_size(struct threadsafeq *q) 
{
	size_t	n;

	if (!q)
		return (0);
	pthread_mutex_lock(&q->lock);
	n = q->n;
	pthread_mutex_unlock(&q->lock);
	return (n);
}



void
threadsafeq_destroy(struct threadsafeq *q)
{

	struct qnode	*node;
	if (!q)
		return ;
	pthread_mutex_lock(&q->lock);
	while (q->head)
	{
	 	node = q->head;
		q->head = q->head->next;
		qnode_destroy(node);
	}
	pthread_mutex_unlock(&q->lock);
	pthread_mutex_destroy(&q->lock);
	free(q);
}

struct threadsafeq	*
threadsafeq_new(void)
{
	struct threadsafeq *q;

	q = malloc(sizeof (*q));
	if (!q)
		goto alloc_err;
	if (0 != pthread_mutex_init(&q->lock, NULL))
		goto mutex_err; 
	q->head = NULL;
	q->tail = NULL;
	q->signal = NULL;
	q->signal_data = NULL;
	q->n = 0;
	return (q);
mutex_err:
	free(q);
alloc_err:
	return (NULL);
}


static void
worker_pool_on_finish(void *data)
{
	struct worker_pool	*pool;

	pool = data;
	pthread_mutex_lock(&pool->lock);
	--((struct worker_pool *)data)->nof_worker;
	pthread_mutex_unlock(&pool->lock); 
}

static void
worker_pool_on_append(void *data)
{
	struct worker_pool	*pool = data;
	pthread_mutex_lock(&pool->lock);
	pthread_cond_signal(&pool->cond);
	pthread_mutex_unlock(&pool->lock);
}

static void	*
worker_pool_loop(void *data)
{
	struct worker_pool	*pool;
	void			*node;
	int			done;

	pthread_cleanup_push(worker_pool_on_finish, data);
	pool = data;
	done = 0;
	while (!done)
	{
		pthread_mutex_lock(&pool->lock);
		done = pool->done;
		pthread_mutex_unlock(&pool->lock);
		if (done)
			break;
		node = threadsafeq_remove(pool->q);
		if (!node)
		{
			pthread_mutex_lock(&pool->lock);
			++pool->idle;
			if (!pool->done)
				pthread_cond_wait(&pool->cond, &pool->lock);
			done = pool->done;
			--pool->idle;
			pthread_mutex_unlock(&pool->lock);
			continue ;
		}
		qnode_exec(node);
	}
	pthread_cleanup_pop(1);
	return (NULL);
}

static int
worker_pool_finish_request(struct worker_pool *pool, size_t timeout_ms)
{

	size_t	timeout_us;
	int	f;

	pthread_mutex_lock(&pool->lock);
	pool->done = 1;
	pthread_cond_broadcast(&pool->cond);
	pthread_mutex_unlock(&pool->lock);
	f = 0;
	timeout_us = timeout_ms * 10;
	do
	{
		pthread_mutex_lock(&pool->lock);
		f = !pool->nof_worker;
		pthread_mutex_unlock(&pool->lock);
		if (f)
			return (0);
		usleep(10);
		timeout_us -= 10;
	} while  (!timeout_us);
	return (-1);
}

int
worker_pool_is_idle(struct worker_pool *pool, size_t timeout_ms)
{
	size_t	timeout_us;
	int	f;

	if (!pool)
		return (0);
	f = 0;
	timeout_us = timeout_ms * 10;
	do
	{
		pthread_mutex_lock(&pool->lock);
		f = ((pool->nof_worker == pool->idle) && !threadsafeq_size(pool->q));
		pthread_mutex_unlock(&pool->lock);
		if (f)
			return (0);
		usleep(10);
		timeout_us -= 10;
	} while  (!timeout_us);
	return (-1);
}


void
worker_pool_destroy(struct worker_pool *pool)
{
	if (!pool)
		return ;
	while (worker_pool_finish_request(pool, 100))
		;
	pool->q->signal = NULL;;
	pool->q->signal_data = NULL;
	pthread_mutex_destroy(&pool->lock);
	pthread_cond_destroy(&pool->cond);
	free(pool);
}


void
worker_pool_append(struct worker_pool *pool, struct qnode *node)
{
	if (!pool)
		return ;
	threadsafeq_append(pool->q, node);
}

struct worker_pool	*
worker_pool_new(struct threadsafeq *q, size_t n)
{
	struct worker_pool		*pool;
	size_t				i;

	if (!q || !n)
		goto ptr_err;
	n = n > QOPS_MAX_WORKER? QOPS_MAX_WORKER: n;
	pool = malloc(sizeof (*pool) + (sizeof (pthread_t) * n));
	*pool = (struct worker_pool){0};
	if (!pool)
		goto alloc_err;
	pool->q = q;
	pool->nof_worker = 0;
	pool->idle = 0;
	pool->done = 0;
	if (0 != pthread_cond_init(&pool->cond, NULL))
		goto cond_err;
	if (0 != pthread_mutex_init(&pool->lock, NULL))
		goto mutex_err;
	i = 0;
	while (i < n)
	{
		if (0 != pthread_create(&pool->tid[i], NULL, &worker_pool_loop, pool))
			goto thread_err;
		pthread_detach(pool->tid[i++]);
		++(pool->nof_worker);
	}
	pool->q->signal = worker_pool_on_append;
	pool->q->signal_data = pool;
	return (pool);
thread_err:
	worker_pool_finish_request(pool, 1000);
	pthread_mutex_destroy(&pool->lock);
mutex_err:
	pthread_cond_destroy(&pool->cond);
cond_err:
	free(pool);
alloc_err:
ptr_err:
	return (NULL);
}



