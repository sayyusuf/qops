#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>

#include <qops.h>

static struct qnode_buff *
qnode_buff_new(size_t size)
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

static void
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
	return (ret);
}

static int
threadsafeq_append_ops(struct threadsafeq *q, struct qnode *node, int signal_f)
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
	if (curr && curr->wi == curr->sz)
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
	if (signal_f && !ret && q->on_append)
		q->on_append(q->signal_data);
	return (ret);
}

int
threadsafeq_append(struct threadsafeq *q, struct qnode *node)
{
	if (!q || !node)
		return (-1);
	return (threadsafeq_append_ops(q, node, 1));
}

int
threadsafeq_append_quiet(struct threadsafeq *q, struct qnode *node)
{
	if (!q || !node)
		return (-1);
	return (threadsafeq_append_ops(q, node, 0));
}

void
threadsafeq_broadcast(struct threadsafeq *q)
{
	if (!q)
		return ;
	if (q->on_broadcast)
		q->on_broadcast(q->signal_data);
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

struct threadsafeq *
threadsafeq_new(size_t buff_sz)
{
	struct threadsafeq	*q;

	q = malloc(sizeof (*q));
	if (!q)
		goto alloc_err;
	if (0 != pthread_mutex_init(&q->lock, NULL))
		goto mutex_err;
	q->head = NULL;
	q->tail = NULL;
	q->on_append = NULL;
	q->on_broadcast = NULL;
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
workerp_on_finish(void *data)
{
	struct workerp	*pool;

	pool = data;
	pthread_mutex_lock(&pool->lock);
	--((struct workerp *)data)->nof_worker;
	pthread_mutex_unlock(&pool->lock);
}

static void
workerp_on_append(void *data)
{
	struct workerp	*pool;

	pool = data;
	pthread_mutex_lock(&pool->lock);
	pthread_cond_signal(&pool->cond);
	pthread_mutex_unlock(&pool->lock);
}

static void
workerp_on_broadcast(void *data)
{
	struct workerp	*pool;

	pool = data;
	pthread_mutex_lock(&pool->lock);
	pthread_cond_broadcast(&pool->cond);
	pthread_mutex_unlock(&pool->lock);
}

static void *
workerp_loop(void *data)
{
	struct workerp	*pool;
	struct qnode	node;
	int		done;
	int		ret;

	pthread_cleanup_push(workerp_on_finish, data);
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
workerp_finish_request(struct workerp *pool, size_t timeout_ms)
{
	int	f;

	pthread_mutex_lock(&pool->lock);
	pool->done = 1;
	pthread_cond_broadcast(&pool->cond);
	pthread_mutex_unlock(&pool->lock);
	f = 0;
	while (1)
	{
		pthread_mutex_lock(&pool->lock);
		f = !pool->nof_worker;
		pthread_mutex_unlock(&pool->lock);
		if (f)
			return (0);
		if (!timeout_ms)
			break ;
		usleep(1000);
		--timeout_ms;
	}
	return (-1);
}

_Bool
workerp_is_idle(struct workerp *pool, size_t timeout_ms)
{
	_Bool	f;
	size_t	i;

	if (!pool)
		return (1);
	f = 0;
	while (1)
	{
		i = 10;
		while (i--)
		{
			pthread_mutex_lock(&pool->lock);
			f = ((pool->nof_worker == pool->idle) && !threadsafeq_size(pool->q));
			pthread_mutex_unlock(&pool->lock);
			if (f || !timeout_ms)
				goto endof_loop;
			usleep(100);
		}
		if (!--timeout_ms)
			break ;
	}
endof_loop:
	return (f);
}

int
workerp_destroy(struct workerp *pool)
{
	if (!pool)
		return (0);
	if (workerp_finish_request(pool, 100))
		return (-1);
	pool->q->on_append = NULL;
	pool->q->on_broadcast = NULL;
	pool->q->signal_data = NULL;
	pthread_mutex_destroy(&pool->lock);
	pthread_cond_destroy(&pool->cond);
	free(pool);
	return (0);
}

void
workerp_broadcast(struct workerp *pool)
{
	workerp_on_broadcast(pool);
}

void
workerp_append(struct workerp *pool, struct qnode *node)
{
	if (!pool)
		return ;
	threadsafeq_append(pool->q, node);
}

void
workerp_append_quiet(struct workerp *pool, struct qnode *node)
{
	if (!pool)
		return ;
	threadsafeq_append_quiet(pool->q, node);
}

struct workerp	*
workerp_new(struct threadsafeq *q, size_t n)
{
	struct workerp	*pool;
	size_t		i;

	if (!q || !n)
		goto ptr_err;
	n = n > QOPS_MAX_WORKER ? QOPS_MAX_WORKER : n;
	pool = malloc(sizeof(*pool) + (sizeof(pthread_t) * n));
	*pool = (struct workerp){0};
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
		if (0 != pthread_create(&pool->tid[i], NULL, &workerp_loop, pool))
			goto thread_err;
		pthread_detach(pool->tid[i++]);
		++(pool->nof_worker);
	}
	pool->q->on_append = workerp_on_append;
	pool->q->on_broadcast = workerp_on_broadcast;
	pool->q->signal_data = pool;
	return (pool);
thread_err:
	workerp_finish_request(pool, 1000);
	pthread_mutex_destroy(&pool->lock);
mutex_err:
	pthread_cond_destroy(&pool->cond);
cond_err:
	free(pool);
alloc_err:
ptr_err:
	return (NULL);
}
