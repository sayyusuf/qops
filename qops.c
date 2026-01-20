#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>
#include "qops.h"

#define THREADSAFEQ_LOCK(q)	pthread_mutex_lock(&q->lock)
#define THREADSAFEQ_UNLOCK(q)	pthread_mutex_unlock(&q->lock)



#define QOPS_PROTO_NAME_DS_READ(name)	int name(struct qops_ds *ds, struct qops_node *node)		
#define QOPS_PROTO_NAME_DS_AT(name)	int name(struct qops_ds *ds, struct qops_node **node, size_t idx)		
#define QOPS_PROTO_NAME_DS_WRITE(name)	int name(struct qops_ds *ds, struct qops_node *node)
#define QOPS_PROTO_NAME_DS_DELETE(name)	void name(struct qops_ds *ds)

#define QOPS_PROTO_DS_READ(name)	QOPS_PROTO_NAME_DS_READ(name##_read)	
#define QOPS_PROTO_DS_AT(name)		QOPS_PROTO_NAME_DS_AT(name##_at)
#define QOPS_PROTO_DS_WRITE(name)	QOPS_PROTO_NAME_DS_WRITE(name##_write)
#define QOPS_PROTO_DS_DELETE(name)	QOPS_PROTO_NAME_DS_DELETE(name##_delete)
#define QOPS_DS_OPS_INIT(name)		(struct qops_ds_ops){.read = name##_read, .at = name##_at, .write = name##_write, .delete = name##_delete}

/* Execute a qops_node */
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
	struct qops_node			node;
	_Atomic (struct qops_node_entry *)	next;
	_Atomic bool				wok;
};

struct qops_node_entry *
qops_node_entry_new(void)
{
	struct qops_node_entry	*entry;

	entry = malloc(sizeof(*entry));
	if (!entry)
		return (NULL);
	*entry = (struct qops_node_entry){.next = NULL, .wok = 0};
	return (entry);
}

		
/* Node buffer structure */
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

static int
qops_node_buff_init(struct qops_node_buff *buff, size_t size)
{
	memset(buff->node_v, 0, sizeof (struct qops_node_entry) * size);
	buff->sz = size;
	atomic_init(&buff->ri, 0);
	atomic_init(&buff->tri, 0);
	atomic_init(&buff->wi, 0);
	atomic_init(&buff->twi, 0);
	atomic_init(&buff->next, NULL);
	return (0);
}

static struct qops_node_buff *
qops_node_buff_new(size_t size)
{
	struct qops_node_buff	*buff;

	if (!size)
		size = QOPS_NODE_BUFF_DEFSIZE;
	buff = malloc(sizeof (*buff) + (sizeof (struct qops_node_entry) * size));
	if (!buff)
		return (NULL);
	qops_node_buff_init(buff, size);
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


/* Data structure operations */
struct qops_ds_ops
{
	QOPS_PROTO_NAME_DS_READ((* read));
	QOPS_PROTO_NAME_DS_AT((* at));
	QOPS_PROTO_NAME_DS_WRITE((* write));
	QOPS_PROTO_NAME_DS_DELETE((* delete));
};




struct qops_ds_buff
{
	_Atomic (struct qops_node_buff *)	head;
	_Atomic (struct qops_node_buff *)	tail;
	_Atomic (struct qops_node_buff *)	root;
};

struct qops_ds_list
{
	_Atomic (struct qops_node_entry *)	head;
	_Atomic (struct qops_node_entry *)	tail;
};

struct qops_ds
{
	_Atomic size_t				n;
	size_t					buff_sz;
	struct qops_ds_ops			ops;
	union
	{
		struct qops_ds_buff		buff;
		struct qops_ds_list		list;
	};
};


/* List data structure operations */
static
QOPS_PROTO_DS_READ(qops_linked_list)
{
	struct qops_node_entry	*next;
	//struct qops_node_entry	*removed;
	_Atomic(struct qops_node_entry	*)head;
	while (1)
	{
		head = atomic_load_explicit(&ds->list.head, memory_order_acquire);
		next = atomic_load_explicit(&head->next, memory_order_acquire);
		if (!next)
			return (-1);
		if (atomic_compare_exchange_strong_explicit(&ds->list.head, &head, next, memory_order_release, memory_order_acquire))
			break ;
	}
	*node = next->node;
	if (!atomic_compare_exchange_strong_explicit(&next->wok, &(bool){0}, 1, memory_order_release, memory_order_acquire))
		free(next);
	if (!atomic_compare_exchange_strong_explicit(&head->wok, &(bool){0}, 1, memory_order_release, memory_order_acquire))
		free(head);
	atomic_fetch_sub_explicit(&ds->n, 1, memory_order_relaxed);
	return (0);
}
static
QOPS_PROTO_DS_AT(qops_linked_list)
{
	(void)ds;
	(void)node;
	(void)idx;
	return (-1);
}

static
QOPS_PROTO_DS_WRITE(qops_linked_list)
{
	struct qops_node_entry	*nd;
	struct qops_node_entry	*tail;
	struct qops_node_entry	*next;
	nd = qops_node_entry_new();
	if (!nd)
		return (-1);
	nd->node = *node;
	while (1)
	{
		tail = atomic_load_explicit(&ds->list.tail, memory_order_acquire);
		next = atomic_load_explicit(&tail->next, memory_order_acquire);
		if (!next)
		{
			if (atomic_compare_exchange_strong_explicit(&tail->next, &(struct qops_node_entry *){NULL}, nd, memory_order_release, memory_order_acquire))
			{
				atomic_compare_exchange_strong_explicit(&ds->list.tail, &tail, nd, memory_order_release, memory_order_acquire);
				break;
			}
		}
		else
			atomic_compare_exchange_strong_explicit(&ds->list.tail, &tail, next, memory_order_release, memory_order_acquire);
	}
	atomic_fetch_add_explicit(&ds->n, 1, memory_order_relaxed);
	return (0);
}

static
QOPS_PROTO_DS_DELETE(qops_linked_list)
{
	_Atomic(struct qops_node_entry	*)head;
	struct qops_node_entry	*next;

	head = atomic_load_explicit(&ds->list.head, memory_order_acquire);
	while (head)
	{
		next = atomic_load_explicit(&head->next, memory_order_acquire);
		free(head);
		head = next;
	}
}

static int
qops_linked_list_init(struct qops_ds *ds, size_t size)
{
	ds->ops = QOPS_DS_OPS_INIT(qops_linked_list);
	ds->list.head = qops_node_entry_new();
	ds->list.tail = ds->list.head;
	(void)size;
	return (0);
}


/* Ring buffer data structure operations */
static
QOPS_PROTO_DS_READ(qops_ring_buff)
{
	(void)ds;
	(void)node;
	return (0);
}

static
QOPS_PROTO_DS_AT(qops_ring_buff)
{
	(void)ds;
	(void)node;
	(void)idx;
	return (-1);
}

static
QOPS_PROTO_DS_WRITE(qops_ring_buff)
{
	(void)ds;
	(void)node;
	return (0);
}

static
QOPS_PROTO_DS_DELETE(qops_ring_buff)
{
	(void)ds;
}

static int
qops_ring_buff_init(struct qops_ds *ds, size_t size)
{
	ds->ops = QOPS_DS_OPS_INIT(qops_ring_buff);
	(void)size;
	return (0);
}


/* Static buffer data structure operations */
static
QOPS_PROTO_DS_READ(qops_static_buff)
{
	(void)ds;
	(void)node;
	return (-1);
}

static
QOPS_PROTO_DS_AT(qops_static_buff)
{
	(void)ds;
	(void)node;
	(void)idx;
	return (-1);
}

static
QOPS_PROTO_DS_WRITE(qops_static_buff)
{
	(void)ds;
	(void)node;
	return (0);
}

static
QOPS_PROTO_DS_DELETE(qops_static_buff)
{
	(void)ds;
}

static int
qops_static_buff_init(struct qops_ds *ds, size_t size)
{
	ds->ops = QOPS_DS_OPS_INIT(qops_static_buff);
	(void)size;
	return (0);
}


/* Linked buffer data structure operations */
static
QOPS_PROTO_DS_READ(qops_linked_buff)
{
	struct qops_node_buff	*head;
	size_t			tri;
	size_t			wi;
	
	while (1)
	{
		head = atomic_load_explicit(&ds->buff.head, memory_order_acquire);
		if (!head)
			return (-1);
		tri = atomic_load_explicit(&head->tri, memory_order_relaxed);
		wi = atomic_load_explicit(&head->wi, memory_order_relaxed);
		if (tri < wi && atomic_compare_exchange_weak_explicit(&head->tri, &tri, tri + 1, memory_order_release, memory_order_acquire))
				break;
		else if (tri >= head->sz)
		{
			struct qops_node_buff *next_nb = atomic_load_explicit(&head->next, memory_order_acquire);
			if (!next_nb)
				return (-1);
			atomic_compare_exchange_strong_explicit(&ds->buff.head, &head, next_nb, memory_order_release, memory_order_acquire);
			//qops_node_buff_delete(nb);
		}
		else if (tri >= wi)
			return (-1);
	}
	while (!atomic_load_explicit(&head->node_v[tri].wok, memory_order_acquire))
		;
	*node = head->node_v[tri].node;
	atomic_fetch_add_explicit(&head->ri, 1, memory_order_relaxed);
	atomic_fetch_sub_explicit(&ds->n, 1, memory_order_relaxed);
	return (0);
}

static
QOPS_PROTO_DS_AT(qops_linked_buff)
{
	(void)ds;
	(void)node;
	(void)idx;
	return (-1);
}

static
QOPS_PROTO_DS_WRITE(qops_linked_buff)
{
	struct qops_node_buff	*tail;
	struct qops_node_buff	*expected;
	struct qops_node_buff	*next_nb;
	size_t			twi;

	while (1)
	{
		tail = atomic_load_explicit(&ds->buff.tail, memory_order_acquire);
		if (tail)
		{
			twi = atomic_load_explicit(&tail->twi, memory_order_relaxed);
			if (twi < tail->sz && atomic_compare_exchange_weak_explicit(&tail->twi, &twi, twi + 1, memory_order_release, memory_order_acquire))
				break;
			else if (twi >= tail->sz)
			{
				next_nb = atomic_load_explicit(&tail->next, memory_order_acquire);
				if (!next_nb)
				{
					next_nb = qops_node_buff_new(ds->buff_sz);
					if (!next_nb)
						return (-1);
					expected = NULL;
					if (!atomic_compare_exchange_weak_explicit(&tail->next, &expected, next_nb, memory_order_release, memory_order_acquire))
					{
						qops_node_buff_delete(next_nb);
						continue ;
					}
				}
				atomic_compare_exchange_weak_explicit(&ds->buff.tail, &tail, next_nb, memory_order_release, memory_order_acquire);
			}
		}
		else
		{
			tail = qops_node_buff_new(ds->buff_sz);
			if (!tail)
				return (-1);
			expected = NULL;
			if (!atomic_compare_exchange_weak_explicit(&ds->buff.tail, &expected, tail, memory_order_release, memory_order_acquire))
				qops_node_buff_delete(tail);
			else
				atomic_store_explicit(&ds->buff.head, tail, memory_order_release);
			continue ;
		}
	}
	tail->node_v[twi].node = *node;
	atomic_store_explicit(&tail->node_v[twi].wok, true, memory_order_release);
	atomic_fetch_add_explicit(&tail->wi, 1, memory_order_relaxed);
	atomic_fetch_add_explicit(&ds->n, 1, memory_order_relaxed);
	return (0);
}

static
QOPS_PROTO_DS_DELETE(qops_linked_buff)
{
	struct qops_node_buff	*nb;
	while (1)
	{
		do
		{
			nb = atomic_load_explicit(&ds->buff.head, memory_order_acquire);
			if (!nb)
				goto eof_loop;
		} while (!atomic_compare_exchange_weak_explicit(&ds->buff.head, &nb, nb->next, memory_order_release, memory_order_acquire));
		qops_node_buff_delete(nb);
	}
eof_loop:
}

static int
qops_linked_buff_init(struct qops_ds *ds, size_t size)
{
	ds->ops = QOPS_DS_OPS_INIT(qops_linked_buff);
	ds->buff.head = qops_node_buff_new(size);
	if (!ds->buff.head)
		return (-1);
	ds->buff.tail = ds->buff.head;
	ds->buff.root = ds->buff.head;
	ds->buff_sz = size;
	return (0);
}


QOPS_PROTO_DS_READ(qops_ds)
{
	if (!ds || !node)
		return (-1);
	return (ds->ops.read(ds, node));
}

QOPS_PROTO_DS_AT(qops_ds)
{
	if (!ds || !node)
		return (-1);
	return (ds->ops.at(ds, node, idx));
}

QOPS_PROTO_DS_WRITE(qops_ds)
{
	if (!ds || !node)
		return (-1);
	return (ds->ops.write(ds, node));
}

//QOPS_PROTO_DS_DELETE(qops_ds)
//{
//	if (!buff)
//		return;
//	return (buff->ops.delete(buff));
//}

QOPS_PROTO_DS_DELETE(qops_ds)
{
	if (!ds)
		return;
	ds->ops.delete(ds);
	free(ds);
}

struct qops_ds *
qops_ds_new(size_t buff_sz, int type)
{
	struct qops_ds	*ds;
	int		ret = 0;

	ds = malloc(sizeof (*ds));
	if (!ds)
		goto alloc_err;
	switch (type)
	{
		case QOPS_LINKED_BUFF:
			ret = qops_linked_buff_init(ds, buff_sz);
			break ;
		case QOPS_STATIC_BUFF:
			ret = qops_static_buff_init(ds, buff_sz);
			break ;
		case QOPS_RING_BUFF:
			ret = qops_ring_buff_init(ds, buff_sz);
			break ;
		case QOPS_LINKED_LIST:
			ret = qops_linked_list_init(ds, buff_sz);
			break ;
		default:
			ret = -1;
	}
	if (ret)
		goto ops_err;
	ds->n = 0;
	atomic_thread_fence(memory_order_seq_cst);
	return (ds);
ops_err:
	free(ds);
alloc_err:
	return (NULL);
}


size_t
qops_ds_size(struct qops_ds *buff)
{
	if (!buff)
		return (0);
	return (atomic_load_explicit(&buff->n, memory_order_acquire));
}





_Thread_local static int qops_wp_local_index = -1;

struct qops_wp
{
	struct qops_ds	*q;
	pthread_cond_t		cond;
	pthread_mutex_t		lock;
	_Atomic size_t		nof_worker;
	_Atomic size_t		idle;
	_Atomic size_t		started;
	_Atomic int		done;
	pthread_t		tid[];
};

static void
qops_wp_on_finish(void *data)
{
	atomic_fetch_sub_explicit(&((struct qops_wp *)data)->nof_worker, 1, memory_order_release);
}

static void
qops_wp_on_write(struct qops_wp *pool)
{
	pthread_mutex_lock(&pool->lock);
	pthread_cond_signal(&pool->cond);
	pthread_mutex_unlock(&pool->lock);
}

static void
qops_wp_on_broadcast(struct qops_wp *pool)
{
	pthread_mutex_lock(&pool->lock);
	pthread_cond_broadcast(&pool->cond);
	pthread_mutex_unlock(&pool->lock);
}

static void
qops_wp_wait(struct qops_wp *pool)
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
qops_wp_wait_and_sz(struct qops_wp *pool)
{
	pthread_mutex_lock(&pool->lock);
	if (!atomic_load_explicit(&pool->done, memory_order_acquire) && !qops_ds_size(pool->q))
	{
		atomic_fetch_add_explicit(&pool->idle, 1, memory_order_relaxed);
		pthread_cond_wait(&pool->cond, &pool->lock);
		atomic_fetch_sub_explicit(&pool->idle, 1, memory_order_relaxed);
	}
	pthread_mutex_unlock(&pool->lock);
}

static void *
qops_wp_loop(void *data)
{
	struct qops_wp	*pool;
	struct qops_node	node;
	int			ret;

	pool = data;
	qops_wp_local_index = atomic_fetch_add_explicit(&pool->started, 1, memory_order_relaxed);
	pthread_cleanup_push(qops_wp_on_finish, data);
	qops_wp_wait(pool);
	while (!atomic_load_explicit(&pool->done, memory_order_acquire))
	{
		ret = qops_ds_read(pool->q, &node);
		if (ret)
			qops_wp_wait_and_sz(pool);
		else
			qops_node_exec(&node);
	}
	pthread_cleanup_pop(1);
	return (NULL);
}

static void *
qops_wp_loop_exec(void *data)
{
	struct qops_wp	*pool;
	size_t			idx;
	struct qops_ds	*buff;
	struct qops_node	*node;

	pool = data;
	qops_wp_local_index = atomic_fetch_add(&pool->started, 1);
	pthread_cleanup_push(qops_wp_on_finish, data);
	qops_wp_wait(pool);
	while (!atomic_load_explicit(&pool->done, memory_order_acquire))
	{
		buff = pool->q;
		idx = qops_wp_local_index;
		if (buff) while (!qops_ds_at(buff, &node, idx))
		{
			qops_node_exec(node);
			idx += pool->nof_worker;
		}
		qops_wp_wait(pool);
	}
	pthread_cleanup_pop(1);
	return (NULL);
}
	
static int
qops_wp_finish_request(struct qops_wp *pool, size_t timeout_ms)
{
	atomic_store_explicit(&pool->done, 1, memory_order_release);
	while (1)
	{
		if (!atomic_load_explicit(&pool->nof_worker, memory_order_acquire))
			return (0);
		else
			qops_wp_on_broadcast(pool);
		if (!timeout_ms)
			break ;
		usleep(1000);
		--timeout_ms;
	}
	return (-1);
}

bool
qops_wp_is_idle(struct qops_wp *pool, size_t timeout_ms)
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
			f = ((atomic_load_explicit(&pool->nof_worker, memory_order_acquire) == atomic_load_explicit(&pool->idle, memory_order_acquire)) && !qops_ds_size(pool->q));
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
qops_wp_get_local_index(void)
{
	return (qops_wp_local_index);
}

int
qops_wp_delete(struct qops_wp *pool)
{
	if (!pool)
		return (0);
	if (qops_wp_finish_request(pool, 100))
		return (-1);
	pthread_mutex_destroy(&pool->lock);
	pthread_cond_destroy(&pool->cond);
	free(pool);
	return (0);
}

int
qops_wp_exec(struct qops_wp *pool, struct qops_ds *buff)
{
	if (!pool || pool->q || !buff || !qops_wp_is_idle(pool, 0))
		return (-1);
	pool->q = buff;
	qops_wp_on_broadcast(pool);
	while (qops_wp_nof_workers(pool) == qops_wp_nof_idle_workers(pool))
		;
	while (!qops_wp_is_idle(pool, 100))
		;
	return (0);
}

void
qops_wp_broadcast(struct qops_wp *pool)
{
	qops_wp_on_broadcast(pool);
}

int
qops_wp_write(struct qops_wp *pool, struct qops_node *node)
{
	int ret = qops_ds_write(pool->q, node);
	if (!ret)
		qops_wp_on_write(pool);
	return (ret);
}

int
qops_wp_write_quiet(struct qops_wp *pool, struct qops_node *node)
{
	return (qops_ds_write(pool->q, node));
}

struct qops_wp	*
qops_wp_new_sched(struct qops_ds *q, size_t n, int sched, int priority)
{
	struct qops_wp	*pool;
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
	*pool = (struct qops_wp){.q = q, .nof_worker = 0, .idle = 0, .started = 0, .done = 0};
	if (0 != pthread_cond_init(&pool->cond, NULL))
		goto cond_err;
	if (0 != pthread_mutex_init(&pool->lock, NULL))
		goto mutex_err;
	i = 0;
	loop = qops_wp_loop_exec;
	if (q)
		loop = qops_wp_loop;
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
	while (qops_wp_finish_request(pool, 1))
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

struct qops_wp	*
qops_wp_new(struct qops_ds *q, size_t n)
{
	return (qops_wp_new_sched(q, n, QOPS_SCHED_OTHER, 0));
}

size_t
qops_wp_nof_workers(struct qops_wp *pool)
{
	return (atomic_load_explicit(&pool->nof_worker, memory_order_acquire));
}

size_t
qops_wp_nof_idle_workers(struct qops_wp *pool)
{
	return (atomic_load_explicit(&pool->idle, memory_order_acquire));
}


#ifdef __cplusplus
}
#endif
