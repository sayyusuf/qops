#ifndef QOPS_H
#define QOPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <pthread.h>
#include <sched.h>

#define QOPS_MAX_WORKER		0xffff
#define QNODE_BUFF_DEFSIZE	64
#define WORKERP_MAX_PRIORITY	99

#define WORKERP_SCHED_OTHER	SCHED_OTHER
#define WORKERP_SCHED_RR	SCHED_RR
#define WORKERP_SCHED_FIFO	SCHED_FIFO

struct qnode
{
	struct qnode	*next;			/* next node */
	void		*data;			/* Data specific to the task */
	int	(*func)(void *data);		/* Task function. First call */
	void	(*err)(void *data, int errcode);/* Error handling callback. Second call if first call returns non-zero. The return value of func is passed as errcode. */
	void	(*cleanup)(void *data);		/* Cleanup callback. Third call */
};

struct qnode_buff
{
	size_t			sz;
	size_t			ri;
	size_t			wi;
	struct qnode_buff	*next;
	struct qnode		nodev[];
};

/**
 * @brief Executes the task in the node.
 *
 * This function executes the `func` of a `qnode`, handling errors and cleaning up afterwards.
 *
 * @param node A pointer to the `qnode` to execute.
 * @return The result of executing the function (0 for success, non-zero for failure).
 */
int
qnode_exec(struct qnode *node);

struct threadsafeq
{
	pthread_mutex_t		lock;
	struct qnode_buff	*head;
	struct qnode_buff	*tail;
	void	(*on_append)(void *);
	void	(*on_broadcast)(void *);
	void			*signal_data;
	size_t			n;
	size_t			buff_sz;
};

/**
 * @brief Appends a node to the thread-safe queue and signals workers.
 *
 * This function adds a node to the queue and signals workers to start processing.
 *
 * @param q A pointer to the `threadsafeq`.
 * @param node A pointer to the `qnode` to add.
 * @return 0 on success, -1 on failure.
 */
int
threadsafeq_append(struct threadsafeq *q, struct qnode *node);

/**
 * @brief Appends a node to the thread-safe queue without signaling workers.
 *
 * This function adds a node to the queue but does not signal workers.
 *
 * @param q A pointer to the `threadsafeq`.
 * @param node A pointer to the `qnode` to add.
 * @return 0 on success, -1 on failure.
 */
int
threadsafeq_append_quiet(struct threadsafeq *q, struct qnode *node);

/**
 * @brief Broadcasts a signal to all workers in the queue.
 *
 * This function signals all workers to wake up and process tasks.
 *
 * @param q A pointer to the `threadsafeq`.
 */
void
threadsafeq_broadcast(struct threadsafeq *q);

/**
 * @brief Removes a node from the thread-safe queue.
 *
 * This function removes the first node in the queue and returns it.
 *
 * @param q A pointer to the `threadsafeq`.
 * @param node A pointer to the `qnode` to store the removed node.
 * @return 0 on success, -1 if the queue is empty.
 */
int
threadsafeq_remove(struct threadsafeq *q, struct qnode *node);

/**
 * @brief Returns the size of the thread-safe queue.
 *
 * This function returns the number of tasks in the queue.
 *
 * @param q A pointer to the `threadsafeq`.
 * @return The size of the queue.
 */
size_t
threadsafeq_size(struct threadsafeq *q);

/**
 * @brief Deletes the thread-safe queue and cleans up all resources.
 *
 * This function frees the memory used by the queue and its buffers.
 *
 * @param q A pointer to the `threadsafeq` to delete.
 */
void
threadsafeq_delete(struct threadsafeq *q);

/**
 * @brief Creates a new thread-safe queue.
 *
 * This function initializes a new `threadsafeq` with the specified buffer size.
 *
 * @param buff_sz The size of the buffer used for each node.
 * @return A pointer to the newly created queue, or NULL if allocation fails.
 */
struct threadsafeq *
threadsafeq_new(size_t buff_sz);

struct workerp
{
	struct threadsafeq	*q;
	pthread_cond_t		cond;
	pthread_mutex_t		lock;
	_Atomic size_t		nof_worker;
	_Atomic size_t		idle;
	_Atomic size_t		started;
	_Atomic int		done;
	_Atomic int		ready;
	pthread_t		tid[];
};

/**
 * @brief Checks if the worker pool is idle.
 *
 * This function checks if all workers are idle and there are no tasks in the queue.
 *
 * @param pool A pointer to the worker pool.
 * @param timeout_ms The timeout in milliseconds.
 * @return 1 if the pool is idle, 0 otherwise.
 */
_Bool
workerp_is_idle(struct workerp *pool, size_t timeout_ms);

/**
 * @brief Deletes the worker pool and cleans up all resources.
 *
 * This function deletes the worker pool, stops all workers in 100 ms, and frees allocated memory.
 *
 * @param pool A pointer to the worker pool to delete.
 * @return 0 on success, -1 on busy.
 */
int
workerp_delete(struct workerp *pool);

/**
 * @brief Broadcasts a signal to all workers in the pool.
 *
 * This function is used to notify all workers to wake up and process tasks.
 *
 * @param pool A pointer to the worker pool.
 */
void
workerp_broadcast(struct workerp *pool);

/**
 * @brief Appends a task to the worker pool's queue.
 *
 * This function adds a task to the queue of the worker pool.
 *
 * @param pool A pointer to the worker pool.
 * @param node A pointer to the `qnode` representing the task.
 */
void
workerp_append(struct workerp *pool, struct qnode *node);

/**
 * @brief Appends a task to the worker pool's queue without signaling workers.
 *
 * This function adds a task to the queue but does not wake up workers.
 *
 * @param pool A pointer to the worker pool.
 * @param node A pointer to the `qnode` representing the task.
 */
void
workerp_append_quiet(struct workerp *pool, struct qnode *node);

/**
 * @brief Creates a new worker pool.
 *
 * This function creates a new worker pool, initializes threads, and prepares the pool for processing tasks.
 *
 * @param q A pointer to the `threadsafeq` for task distribution.
 * @param n The number of worker threads to create.
 * @return A pointer to the newly created worker pool, or NULL if allocation fails.
 */
struct workerp *
workerp_new(struct threadsafeq *q, size_t n);

/**
 * @brief Creates a new worker pool with custom scheduling parameters.
 *
 * This function creates a new worker pool similar to workerp_new(), but allows specifying
 * a custom thread scheduling policy and priority for each worker.
 *
 * Supported scheduling policies:
 * - WORKERP_SCHED_OTHER (default time-sharing policy)
 * - WORKERP_SCHED_RR (real-time round-robin)
 * - WORKERP_SCHED_FIFO (real-time first-in-first-out)
 *
 * @param q A pointer to the `threadsafeq` for task distribution.
 * @param n The number of worker threads to create.
 * @param sched The scheduling policy to use for worker threads. Use one of the WORKERP_SCHED_* macros.
 * @param priority The thread priority (used with real-time policies such as RR or FIFO). Ignored for SCHED_OTHER.
 * @return A pointer to the newly created worker pool, or NULL if allocation or thread setup fails.
 */
struct workerp *
workerp_new_sched(struct threadsafeq *q, size_t n, int sched, int priority);

/**
 * @brief Gets the index of the currently executing worker thread.
 *
 * Returns a thread-local index that uniquely identifies the calling thread
 * within its worker pool. This is useful for assigning per-thread data or tasks.
 *
 * @return The index of the current worker thread, or -1 if called from a non-worker thread.
 */
int
workerp_get_local_index(void);

#ifdef __cplusplus
}
#endif

#endif
