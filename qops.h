#ifndef QOPS_H
#define  QOPS_H

#include <stddef.h>

#define QOPS_MAX_WORKER	0xffff
#define QNODE_BUFF_DEFSIZE	64;

struct qnode
{
	struct	qnode	*next;
	int	(*func)(void *);
	void	(*cleanup)(void *);
	void	(*err)(void *, int);
	void	*data;
};

struct	qnode_buff
{
	size_t			sz;
	size_t			ri;
	size_t			wi;
	struct qnode_buff	*next;
	struct qnode		nodev[];
};

int
qnode_exec(struct qnode *node);

struct threadsafeq
{
	pthread_mutex_t		lock;
	struct qnode_buff	*head;
	struct qnode_buff	*tail;
	void			(*signal)(void *);
	void			*signal_data;
	size_t			n;
	size_t			buff_sz;
};

int
threadsafeq_append(struct threadsafeq *q, struct qnode *node);

int
threadsafeq_remove(struct threadsafeq *q, struct qnode *node);

size_t
threadsafeq_size(struct threadsafeq *q);

void
threadsafeq_destroy(struct threadsafeq *q);

struct threadsafeq	*
threadsafeq_new(size_t buff_sz);


struct worker_pool
{
	struct threadsafeq	*q;
	pthread_cond_t		cond;
	pthread_mutex_t		lock;
	volatile size_t		nof_worker;
	volatile size_t		idle;
	volatile int		done;
	pthread_t		tid[];
};

int
worker_pool_is_idle(struct worker_pool *pool, size_t timeout_ms);

void
worker_pool_destroy(struct worker_pool *pool);

void
worker_pool_append(struct worker_pool *pool, struct qnode *node);

struct worker_pool	*
worker_pool_new(struct threadsafeq *q, size_t n);

#endif

