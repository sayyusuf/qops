#include <pthread.h> 
#include <unistd.h> 
#include <stdlib.h>
#include <stddef.h>

#include <qops.h>

struct qnode_buff	*
qnode_buff_new(size_t	size)
{
	struct qnode_buff	*qbuff;

	if (!size)
		size = QNODE_BUFF_DEFSIZE;
	qbuff = malloc(sizeof (*qbuff) + (sizeof (struct qnode) * size));
	if (!qbuff)
		return (NULL);
	qbuff->sz = size;
	qbuff->ri = 0;
	qbuff->wi = 0;
	qbuff->next = NULL;
	return (qbuff);
}

void
qnode_buff_destroy(struct qnode_buff *qbuff)
{
	struct qnode	*node;
	if (!qbuff)
		return ;
	while (qbuff->ri < qbuff->wi)
	{
		node = qbuff->nodev + qbuff->ri++;
		if (node->cleanup)
			node->cleanup(node->data);
	}
	free(qbuff);
}


int
qnode_exec(struct qnode *node)
{
	int	ret;

	ret = 0;
	if (node->func)
	{
		 ret = node->func(node->data);
		 if (node->err && ret)
			 node->err(node->data, ret);
	}
	if (node->cleanup)
		node->cleanup(node->data);
	return  (ret);
}

int
threadsafeq_append(struct threadsafeq *q, struct qnode *node)
{
	struct qnode_buff	*curr;
	int			ret;

	if (!q || !node)
		return (-1);
	ret = 0;
	pthread_mutex_lock(&q->lock);
	if (!q->head)
	{
		q->head = qnode_buff_new(q->buff_sz);
		q->tail = q->head;
		q->n = 0;
	}
	curr = q->tail;
	if (curr  && curr->wi == curr->sz)
	{
		curr->next = qnode_buff_new(q->buff_sz);
		curr = curr->next;
	}
	if (curr)
	{
		q->tail = curr;
		curr->nodev[curr->wi++] = *node;
		++q->n;
	}
	else
		ret = -1;
	pthread_mutex_unlock(&q->lock);
	if (!ret && q->signal)
		q->signal(q->signal_data);
	return (ret);
}

int
threadsafeq_remove(struct threadsafeq *q, struct qnode *node) 
{
	struct qnode_buff	*curr;
	int			ret;

	if (!q || !node)
		return (-1);
	ret = 0;
	pthread_mutex_lock(&q->lock);
	if (q->n)
	{
		curr = q->head;
		*node = curr->nodev[curr->ri++];
		--q->n;
		if (curr->wi == curr->ri)
		{
			q->head = curr->next;
			qnode_buff_destroy(curr);
		}
	}
	else
		ret = -1;
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

	struct qnode_buff	*buff;
	if (!q)
		return ;
	pthread_mutex_lock(&q->lock);
	while (q->head)
	{
	 	buff = q->head;
		q->head = q->head->next;
		qnode_buff_destroy(buff);
	}
	pthread_mutex_unlock(&q->lock);
	pthread_mutex_destroy(&q->lock);
	free(q);
}

struct threadsafeq	*
threadsafeq_new(size_t buff_sz)
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
	q->buff_sz = buff_sz;
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
	struct worker_pool	*pool;

	pool = data;
	pthread_mutex_lock(&pool->lock);
	pthread_cond_signal(&pool->cond);
	pthread_mutex_unlock(&pool->lock);
}

static void	*
worker_pool_loop(void *data)
{
	struct worker_pool	*pool;
	struct qnode		node;
	int			done;
	int			ret;
	pthread_cleanup_push(worker_pool_on_finish, data);
	pool = data;
	done = 0;
	while (!done)
	{
		pthread_mutex_lock(&pool->lock);
		done = pool->done;
		if (!done)
		{
			ret = threadsafeq_remove(pool->q, &node);
			if (ret)
			{
				++pool->idle;
				pthread_cond_wait(&pool->cond, &pool->lock);
				done = pool->done;
				--pool->idle;
			}
		}
		pthread_mutex_unlock(&pool->lock);
		if (!ret && !done) 
			qnode_exec(&node);
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
	while (1)
	{
		pthread_mutex_lock(&pool->lock);
		f = !pool->nof_worker;
		pthread_mutex_unlock(&pool->lock);
		if (f)
			return (0);
		if (!timeout_us)
			break ;
		usleep(10);
		timeout_us -= 10;
	}
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
	while (1)
	{
		pthread_mutex_lock(&pool->lock);
		f = ((pool->nof_worker == pool->idle) && !threadsafeq_size(pool->q));
		pthread_mutex_unlock(&pool->lock);
		if (f)
			return (1);
		if (!timeout_us)
			break ;
		usleep(10);
		timeout_us -= 10;
	}
	return (0);
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



