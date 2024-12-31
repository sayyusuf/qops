#ifndef QOPS_H
#define  QOPS_H

#include <stddef.h>

#define QOPS_MAX_WORKER	0xffff

struct qnode
{
	struct	qnode	*next;
	void	(*cleanup_node)(void *);
	int	(*func)(void *);
	void	(*cleanup)(void *);
	void	(*err)(void *, int);
	void	*data;
};

void
qnode_destroy(struct qnode *node);

int
qnode_exec(struct qnode *node);

struct qnode	*
qnode_new(void *data, int (*func)(void *), void (*cleanup)(void *), void (*err)(void *, int));


struct threadsafeq
{
	pthread_mutex_t	lock;
	struct qnode	*head;
	struct qnode	*tail;
	void		(*signal)(void *);
	void		*signal_data;
	size_t		n;		
};

void
threadsafeq_append(struct threadsafeq *q, struct qnode *new);

struct qnode	*
threadsafeq_remove(struct threadsafeq *q);

size_t
threadsafeq_size(struct threadsafeq *q);

void
threadsafeq_destroy(struct threadsafeq *q);

struct threadsafeq	*
threadsafeq_new(void);


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

