#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdatomic.h>
#include "qops.h"

#define THREADSAFEQ_LOCK(q)	pthread_mutex_lock(&q->lock)
#define THREADSAFEQ_UNLOCK(q)	pthread_mutex_unlock(&q->lock)



#define QOPS_PROTO_NAME_BUFF_READ(name)		int name(struct qops_buff *buff, struct qops_node *node)		
#define QOPS_PROTO_NAME_BUFF_AT(name)		int name(struct qops_buff *buff, struct qops_node **node, size_t idx)		
#define QOPS_PROTO_NAME_BUFF_WRITE(name)	int name(struct qops_buff *buff, struct qops_node *node)
#define QOPS_PROTO_NAME_BUFF_DELETE(name)	void name(struct qops_buff *buff)

#define QOPS_PROTO_BUFF_READ(name)		QOPS_PROTO_NAME_BUFF_READ(name##_read)	
#define QOPS_PROTO_BUFF_AT(name)		QOPS_PROTO_NAME_BUFF_AT(name##_at)
#define QOPS_PROTO_BUFF_WRITE(name)		QOPS_PROTO_NAME_BUFF_WRITE(name##_write)
#define QOPS_PROTO_BUFF_DELETE(name)		QOPS_PROTO_NAME_BUFF_DELETE(name##_delete)
#define qops_BUFF_OPS_INIT(name)		{.read = name##_read, .at = name##_at, .write = name##_write, .delete = name##_delete}

int
qops_node_exec(struct qops_node *node)
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


struct qops_node_entry
{
	struct qops_node		node;
	_Atomic bool			wok;
};

struct qops_node_buff
{
	size_t					sz;
	_Atomic size_t				ri;
	_Atomic size_t				tri;
	_Atomic size_t				wi;
	_Atomic size_t				twi;
	_Atomic (struct qops_node_buff *)	next;
	struct qops_node_entry			node_v[];
};

static struct qops_node_buff *
qops_node_buff_new(size_t size)
{
	struct qops_node_buff	*buff;

	if (!size)
		size = QOPS_NODE_BUFF_DEFSIZE;
	buff = calloc(sizeof (*buff) + (sizeof (struct qops_node_entry) * size), 1);
	if (!buff)
		return (NULL);
	buff->sz = size;
	buff->ri = 0;
	buff->tri = 0;
	buff->wi = 0;
	buff->twi = 0;
	buff->next = NULL;
	return (buff);
}

static void
qops_node_buff_delete(struct qops_node_buff *buff)
{
	struct qops_node_entry	*entry;

	if (!buff)
		return ;
	while (buff->ri != buff->wi)
	{
		if (buff->ri >= buff->sz)
			buff->ri = buff->ri % buff->sz;
		entry = buff->node_v + buff->ri++;
		if (entry->node.cleanup)
			entry->node.cleanup(entry->node.data);
	}
	free(buff);
}


struct qops_buff_ops
{
	QOPS_PROTO_NAME_BUFF_READ((* read));
	QOPS_PROTO_NAME_BUFF_AT((* at));
	QOPS_PROTO_NAME_BUFF_WRITE((* write));
	QOPS_PROTO_NAME_BUFF_DELETE((* delete));
};


struct qops_buff
{
	struct qops_buff_ops			ops;
	_Atomic (struct qops_node_buff *)	head;
	_Atomic (struct qops_node_buff *)	tail;
	_Atomic (struct qops_node_buff *)	delete;
	_Atomic size_t				n;
	size_t					buff_sz;
	pthread_mutex_t				mtx;
};


static
QOPS_PROTO_BUFF_READ(qops_ring_buff)
{
	(void)buff;
	(void)node;
	return (0);
}

static
QOPS_PROTO_BUFF_AT(qops_ring_buff)
{
	(void)buff;
	(void)node;
	(void)idx;
	return (-1);
}

static
QOPS_PROTO_BUFF_WRITE(qops_ring_buff)
{
	(void)buff;
	(void)node;
	return (0);
}

static
QOPS_PROTO_BUFF_DELETE(qops_ring_buff)
{
	(void)buff;
}

static struct qops_buff_ops ring_buff_ops = qops_BUFF_OPS_INIT(qops_ring_buff);

static
QOPS_PROTO_BUFF_READ(qops_static_buff)
{
	(void)buff;
	(void)node;
	return (-1);
}

static
QOPS_PROTO_BUFF_AT(qops_static_buff)
{
	(void)buff;
	(void)node;
	(void)idx;
	return (-1);
}

static
QOPS_PROTO_BUFF_WRITE(qops_static_buff)
{
	(void)buff;
	(void)node;
	return (0);
}

static
QOPS_PROTO_BUFF_DELETE(qops_static_buff)
{
	(void)buff;
}

static struct qops_buff_ops static_buff_ops = qops_BUFF_OPS_INIT(qops_static_buff);

#include <stdio.h>

static
QOPS_PROTO_BUFF_READ(qops_linked_buff)
{
	struct qops_node_buff	*nb;
	size_t			tri;
	size_t			wi;
	
	while (1)
	{
		nb = atomic_load_explicit(&buff->head, memory_order_acquire);
		if (!nb)
			return (-1);
		tri = atomic_load_explicit(&nb->tri, memory_order_relaxed);
		wi = atomic_load_explicit(&nb->wi, memory_order_relaxed);
		if (tri < wi && atomic_compare_exchange_weak_explicit(&nb->tri, &tri, tri + 1, memory_order_release, memory_order_acquire))
				break;
		else if (tri >= nb->sz)
		{
			struct qops_node_buff *next_nb = atomic_load_explicit(&nb->next, memory_order_acquire);
			if (!next_nb)
				return (-1);
			atomic_compare_exchange_strong_explicit(&buff->head, &nb, next_nb, memory_order_release, memory_order_acquire);
			//qops_node_buff_delete(nb);
		}
		else if (tri >= wi)
			return (-1);
	}
	while (!atomic_load_explicit(&nb->node_v[tri].wok, memory_order_acquire))
		;
	*node = nb->node_v[tri].node;
	atomic_fetch_add_explicit(&nb->ri, 1, memory_order_release);
	atomic_fetch_sub_explicit(&buff->n, 1, memory_order_relaxed);
	return (0);
}

static
QOPS_PROTO_BUFF_AT(qops_linked_buff)
{
	(void)buff;
	(void)node;
	(void)idx;
	return (-1);
}

static
QOPS_PROTO_BUFF_WRITE(qops_linked_buff)
{
	struct qops_node_buff	*nb;
	struct qops_node_buff	*expected;
	struct qops_node_buff	*next_nb;
	size_t			twi;

	while (1)
	{
		nb = atomic_load_explicit(&buff->tail, memory_order_acquire);
		if (nb)
		{
			twi = atomic_load_explicit(&nb->twi, memory_order_relaxed);
			if (twi < nb->sz && atomic_compare_exchange_weak_explicit(&nb->twi, &twi, twi + 1, memory_order_release, memory_order_acquire))
				break;
			else if (twi >= nb->sz)
			{
				next_nb = atomic_load_explicit(&nb->next, memory_order_acquire);
				if (!next_nb)
				{
					next_nb = qops_node_buff_new(buff->buff_sz);
					if (!next_nb)
						return (-1);
					expected = NULL;
					if (!atomic_compare_exchange_weak_explicit(&nb->next, &expected, next_nb, memory_order_release, memory_order_acquire))
					{
						qops_node_buff_delete(next_nb);
						continue ;
					}
				}
				atomic_compare_exchange_weak_explicit(&buff->tail, &nb, next_nb, memory_order_release, memory_order_acquire);
			}
		}
		else
		{
			nb = qops_node_buff_new(buff->buff_sz);
			if (!nb)
				return (-1);
			expected = NULL;
			if (!atomic_compare_exchange_weak_explicit(&buff->tail, &expected, nb, memory_order_release, memory_order_acquire))
				qops_node_buff_delete(nb);
			else
				atomic_store_explicit(&buff->head, nb, memory_order_release);
			continue ;
		}
	}
	nb->node_v[twi].node = *node;
	nb->node_v[twi].wok = true;
	atomic_fetch_add_explicit(&nb->wi, 1, memory_order_release);
	atomic_fetch_add_explicit(&buff->n, 1, memory_order_relaxed);
	return (0);
}

static
QOPS_PROTO_BUFF_DELETE(qops_linked_buff)
{
	(void)buff;
}

static struct qops_buff_ops linked_buff_ops = qops_BUFF_OPS_INIT(qops_linked_buff);


QOPS_PROTO_BUFF_READ(qops_buff)
{
	if (!buff || !node)
		return (-1);
	return (buff->ops.read(buff, node));
}

QOPS_PROTO_BUFF_AT(qops_buff)
{
	if (!buff || !node)
		return (-1);
	return (buff->ops.at(buff, node, idx));
}

QOPS_PROTO_BUFF_WRITE(qops_buff)
{
	if (!buff || !node)
		return (-1);
	return (buff->ops.write(buff, node));
}

//QOPS_PROTO_BUFF_DELETE(qops_buff)
//{
//	if (!buff)
//		return;
//	return (buff->ops.delete(buff));
//}

QOPS_PROTO_BUFF_DELETE(qops_buff)
{
	struct qops_node_buff	*nb;
	while (1)
	{
		do
		{
			nb = atomic_load_explicit(&buff->head, memory_order_acquire);
			if (!nb)
				goto eof_loop;
		} while (!atomic_compare_exchange_weak_explicit(&buff->head, &nb, nb->next, memory_order_release, memory_order_acquire));
		qops_node_buff_delete(nb);
	}
eof_loop:
	pthread_mutex_destroy(&buff->mtx);
	free(buff);
}

struct qops_buff *
qops_buff_new(size_t buff_sz, int type)
{
	struct qops_buff	*q;

	q = malloc(sizeof (*q) + buff_sz * sizeof (void *));
	if (!q)
		goto alloc_err;
	switch (type)
	{
		case QOPS_LINKED_BUFF:
			q->ops = linked_buff_ops;
			break ;
		case QOPS_STATIC_BUFF:
			q->ops = static_buff_ops;
			break ;
		case QOPS_RING_BUFF:
			q->ops = ring_buff_ops;
			break ;
		default:
			goto type_err;
	}
	q->head = qops_node_buff_new(buff_sz);
	if (!q->head)
		goto alloc_node_buff_err;
	q->tail = q->head;
	q->delete = NULL;
	q->buff_sz = buff_sz;
	q->n = 0;
	if (0 != pthread_mutex_init(&q->mtx, NULL))
		goto mutex_err;
	atomic_thread_fence(memory_order_seq_cst);
	return (q);
mutex_err:
	qops_node_buff_delete(q->head);
alloc_node_buff_err:
type_err:
	free(q);
alloc_err:
	return (NULL);
}


size_t
qops_buff_size(struct qops_buff *buff)
{
	if (!buff)
		return (0);
	return (atomic_load_explicit(&buff->n, memory_order_acquire));
}





_Thread_local static int qops_pool_local_index = -1;

struct qops_pool
{
	struct qops_buff	*q;
	pthread_cond_t		cond;
	pthread_mutex_t		lock;
	_Atomic size_t		nof_worker;
	_Atomic size_t		idle;
	_Atomic size_t		started;
	_Atomic int		done;
	pthread_t		tid[];
};

static void
qops_pool_on_finish(void *data)
{
	atomic_fetch_sub_explicit(&((struct qops_pool *)data)->nof_worker, 1, memory_order_release);
}

static void
qops_pool_on_write(struct qops_pool *pool)
{
	pthread_mutex_lock(&pool->lock);
	pthread_cond_signal(&pool->cond);
	pthread_mutex_unlock(&pool->lock);
}

static void
qops_pool_on_broadcast(struct qops_pool *pool)
{
	pthread_mutex_lock(&pool->lock);
	pthread_cond_broadcast(&pool->cond);
	pthread_mutex_unlock(&pool->lock);
}

static void
qops_pool_wait(struct qops_pool *pool)
{
	pthread_mutex_lock(&pool->lock);
	if (!atomic_load_explicit(&pool->done, memory_order_acquire))
	{
		atomic_fetch_add_explicit(&pool->idle, 1, memory_order_relaxed);
		pthread_cond_wait(&pool->cond, &pool->lock);
		atomic_fetch_sub_explicit(&pool->idle, 1, memory_order_relaxed);
	}
	pthread_mutex_unlock(&pool->lock);
}

static void
qops_pool_wait_and_sz(struct qops_pool *pool)
{
	pthread_mutex_lock(&pool->lock);
	if (!atomic_load_explicit(&pool->done, memory_order_acquire) && !qops_buff_size(pool->q))
	{
		atomic_fetch_add_explicit(&pool->idle, 1, memory_order_relaxed);
		pthread_cond_wait(&pool->cond, &pool->lock);
		atomic_fetch_sub_explicit(&pool->idle, 1, memory_order_relaxed);
	}
	pthread_mutex_unlock(&pool->lock);
}

static void *
qops_pool_loop(void *data)
{
	struct qops_pool	*pool;
	struct qops_node	node;
	int			ret;

	pool = data;
	qops_pool_local_index = atomic_fetch_add_explicit(&pool->started, 1, memory_order_relaxed);
	pthread_cleanup_push(qops_pool_on_finish, data);
	qops_pool_wait(pool);
	while (!atomic_load_explicit(&pool->done, memory_order_acquire))
	{
		ret = qops_buff_read(pool->q, &node);
		if (ret)
			qops_pool_wait_and_sz(pool);
		else
			qops_node_exec(&node);
	}
	pthread_cleanup_pop(1);
	return (NULL);
}

static void *
qops_pool_loop_exec(void *data)
{
	struct qops_pool	*pool;
	size_t			idx;
	struct qops_buff	*buff;
	struct qops_node	*node;

	pool = data;
	qops_pool_local_index = atomic_fetch_add(&pool->started, 1);
	pthread_cleanup_push(qops_pool_on_finish, data);
	qops_pool_wait(pool);
	while (!atomic_load_explicit(&pool->done, memory_order_acquire))
	{
		buff = pool->q;
		idx = qops_pool_local_index;
		if (buff) while (!qops_buff_at(buff, &node, idx))
		{
			qops_node_exec(node);
			idx += pool->nof_worker;
		}
		qops_pool_wait(pool);
	}
	pthread_cleanup_pop(1);
	return (NULL);
}
	
static int
qops_pool_finish_request(struct qops_pool *pool, size_t timeout_ms)
{
	atomic_store_explicit(&pool->done, 1, memory_order_release);
	while (1)
	{
		if (!atomic_load_explicit(&pool->nof_worker, memory_order_acquire))
			return (0);
		else
			qops_pool_on_broadcast(pool);
		if (!timeout_ms)
			break ;
		usleep(1000);
		--timeout_ms;
	}
	return (-1);
}

bool
qops_pool_is_idle(struct qops_pool *pool, size_t timeout_ms)
{
	bool	f;
	size_t	i;

	if (!pool)
		return (1);
	f = 0;
	while (1)
	{
		i = 10;
		while (i--)
		{
			f = ((atomic_load_explicit(&pool->nof_worker, memory_order_acquire) == atomic_load_explicit(&pool->idle, memory_order_acquire)) && !qops_buff_size(pool->q));
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
qops_pool_get_local_index(void)
{
	return (qops_pool_local_index);
}

int
qops_pool_delete(struct qops_pool *pool)
{
	if (!pool)
		return (0);
	if (qops_pool_finish_request(pool, 100))
		return (-1);
	pthread_mutex_destroy(&pool->lock);
	pthread_cond_destroy(&pool->cond);
	free(pool);
	return (0);
}

int
qops_pool_exec(struct qops_pool *pool, struct qops_buff *buff)
{
	if (!pool || pool->q || !buff || !qops_pool_is_idle(pool, 0))
		return (-1);
	pool->q = buff;
	qops_pool_on_broadcast(pool);
	while (qops_pool_nof_workers(pool) == qops_pool_nof_idle_workers(pool))
		;
	while (!qops_pool_is_idle(pool, 100))
		;
	return (0);
}

void
qops_pool_broadcast(struct qops_pool *pool)
{
	qops_pool_on_broadcast(pool);
}

int
qops_pool_write(struct qops_pool *pool, struct qops_node *node)
{
	int ret = qops_buff_write(pool->q, node);
	if (!ret)
		qops_pool_on_write(pool);
	return (ret);
}

int
qops_pool_write_quiet(struct qops_pool *pool, struct qops_node *node)
{
	return (qops_buff_write(pool->q, node));
}

struct qops_pool	*
qops_pool_new_sched(struct qops_buff *q, size_t n, int sched, int priority)
{
	struct qops_pool	*pool;
	pthread_attr_t		attr;
	struct sched_param	param;
	size_t			i;
	void *(*loop)(void*);

	if (!n)
		goto sz_err;
	n = n > QOPS_MAX_WORKER ? QOPS_MAX_WORKER : n;
	if (pthread_attr_init(&attr))
		goto attr_err;
	if (pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED))
		goto set_attr_err;
	if (pthread_attr_setschedpolicy(&attr, sched))
		goto set_attr_err;
	param.sched_priority = priority > QOPS_MAX_PRIORITY? QOPS_MAX_PRIORITY: priority;
	if (pthread_attr_setschedparam(&attr, &param))
		goto set_attr_err;
	pool = malloc(sizeof(*pool) + (sizeof(pthread_t) * n));
	if (!pool)
		goto alloc_err;
	*pool = (struct qops_pool){.q = q, .nof_worker = 0, .idle = 0, .started = 0, .done = 0};
	if (0 != pthread_cond_init(&pool->cond, NULL))
		goto cond_err;
	if (0 != pthread_mutex_init(&pool->lock, NULL))
		goto mutex_err;
	i = 0;
	loop = qops_pool_loop_exec;
	if (q)
		loop = qops_pool_loop;
	while (i < n)
	{
		if (0 != pthread_create(&pool->tid[i], &attr, loop, pool))
			goto thread_err;
		pthread_detach(pool->tid[i++]);
		atomic_fetch_add(&pool->nof_worker, 1);
	}
	pthread_attr_destroy(&attr);
	while (atomic_load_explicit(&pool->nof_worker, memory_order_acquire) != atomic_load_explicit(&pool->idle, memory_order_acquire))
		usleep(100);
	return (pool);
thread_err:
	while (qops_pool_finish_request(pool, 1))
		;
	pthread_mutex_destroy(&pool->lock);
mutex_err:
	pthread_cond_destroy(&pool->cond);
cond_err:
alloc_err:
set_attr_err:
	pthread_attr_destroy(&attr);
attr_err:
sz_err:
	return (NULL);
}

struct qops_pool	*
qops_pool_new(struct qops_buff *q, size_t n)
{
	return (qops_pool_new_sched(q, n, QOPS_SCHED_OTHER, 0));
}

size_t
qops_pool_nof_workers(struct qops_pool *pool)
{
	return (atomic_load_explicit(&pool->nof_worker, memory_order_acquire));
}

size_t
qops_pool_nof_idle_workers(struct qops_pool *pool)
{
	return (atomic_load_explicit(&pool->idle, memory_order_acquire));
}


#ifdef __cplusplus
}
#endif
