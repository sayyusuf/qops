#ifndef qops_H
#define qops_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>

#define QOPS_MAX_WORKER		0xffff
#define QOPS_MAX_PRIORITY	99
#define QOPS_NODE_BUFF_DEFSIZE	64

#define QOPS_SCHED_OTHER	SCHED_OTHER
#define QOPS_SCHED_RR		SCHED_RR
#define QOPS_SCHED_FIFO		SCHED_FIFO

#define QOPS_LINKED_BUFF	0
#define QOPS_STATIC_BUFF	1
#define QOPS_RING_BUFF		2
#define QOPS_LINKED_LIST_LF	3


#define QOPS_DEFAULT_OPS	QOPS_LINKED_LIST_LF

struct qops_node
{
	void	*data;				/* Data specific to the task */
	int	(*func)(void *data);		/* Task function. First call */
	void	(*err)(void *data, int errcode);/* Error handling callback. Second call if first call returns non-zero. The return value of func is passed as errcode. */
	void	(*cleanup)(void *data);		/* Cleanup callback. Third call */
};

/**
 * @brief Executes the task in the node.
 *
 * This function executes the `func` of a `qops_node`, handling errors and cleaning up afterwards.
 *
 * @param node A pointer to the `qops_node` to execute.
 * @return The result of executing the function (0 for success, non-zero for failure).
 */
int
qops_node_exec(struct qops_node *node);

struct qops_ds;

/**
 * @brief Write a node to the thread-safe buffer and signal workers.
 *
 * Adds a node to the buffer and signals workers to start processing.
 *
 * @param q Pointer to the `qops_ds`.
 * @param node Pointer to the `qops_node` to add.
 * @return 0 on success, -1 on failure.
 */
int
qops_ds_write(struct qops_ds *q, struct qops_node *node);

/**
 * @brief Read a node from the thread-safe buffer.
 *
 * Removes the first node from the buffer and stores it in `node`.
 *
 * @param q Pointer to the `qops_ds`.
 * @param node Pointer to the `qops_node` where the read node will be stored.
 * @return 0 on success, -1 if the buffer is empty.
 */
int
qops_ds_read(struct qops_ds *q, struct qops_node *node);


/**
 * @brief Read a node at index from the thread-safe buffer.
 *
 * Reads the node at `idx` from the buffer (non-destructive access) and
 * stores a pointer to it in `node`.
 *
 * @param q Pointer to the `qops_ds`.
 * @param node Pointer to a `qops_node *` that will receive the address of the node.
 * @param idx Index of the node to read.
 * @return 0 on success, -1 if the index is out of range or on error.
 */
int
qops_ds_at(struct qops_ds *q, struct qops_node **node, size_t idx);



/**
 * @brief Returns the size of the thread-safe buffer.
 *
 * This function returns the number of tasks in the buffer.
 *
 * @param q A pointer to the `qops_ds`.
 * @return The size of the buffer.
 */
size_t
qops_ds_size(struct qops_ds *q);

/**
 * @brief Delete the thread-safe buffer and release resources.
 *
 * Frees memory used by the buffer and any internal resources.
 *
 * @param q Pointer to the `qops_ds` to delete.
 */
void
qops_ds_delete(struct qops_ds *q);

/**
 * @brief Create a new thread-safe buffer.
 *
 * Initializes a new `qops_ds` with the specified buffer size and
 * implementation type.
 *
 * @param buff_sz The size of the buffer (number of entries). If 0, a default
 *                size is used.
 * @param type   Implementation type: `QOPS_LINKED_BUFF`, `qops_STATIC_BUFF`,
 *               or `QOPS_RING_BUFF`.
 * @return Pointer to the newly created buffer, or NULL on allocation failure.
 */
struct qops_ds *
qops_ds_new(size_t buff_sz, int type);






struct qops_wp;

/**
 * @brief Check if the worker pool is idle.
 *
 * Returns true when all workers are idle and there are no tasks in the buffer.
 *
 * @param pool A pointer to the worker pool.
 * @param timeout_ms The timeout in milliseconds.
 * @return 1 if the pool is idle, 0 otherwise.
 */
bool
qops_wp_is_idle(struct qops_wp *pool, size_t timeout_ms);

/**
 * @brief Deletes the worker pool and cleans up all resources.
 *
 * This function deletes the worker pool, stops all workers in 100 ms, and frees allocated memory.
 *
 * @param pool A pointer to the worker pool to delete.
 * @return 0 on success, -1 on busy.
 */
int
qops_wp_delete(struct qops_wp *pool);

/**
 * @brief Broadcasts a signal to all workers in the pool.
 *
 * This function is used to notify all workers to wake up and process tasks.
 *
 * @param pool A pointer to the worker pool.
 */
void
qops_wp_broadcast(struct qops_wp *pool);

/**
 * @brief Write a task to the worker pool's buffer.
 *
 * Adds a task to the pool's buffer and signals workers (if applicable).
 *
 * @param pool Pointer to the worker pool.
 * @param node Pointer to the `qops_node` representing the task.
 * @return 0 on success, -1 on failure.
 */
int
qops_wp_write(struct qops_wp *pool, struct qops_node *node);

/**
 * @brief Write a task to the worker pool's buffer without signaling workers.
 *
 * Adds a task to the buffer but does not wake up workers.
 *
 * @param pool Pointer to the worker pool.
 * @param node Pointer to the `qops_node` representing the task.
 * @return 0 on success, -1 on failure.
 */
int
qops_wp_write_quiet(struct qops_wp *pool, struct qops_node *node);

/**
 * @brief Creates a new worker pool.
 *
 * This function creates a new worker pool, initializes threads, and prepares the pool for processing tasks.
 *
 * @param q A pointer to the `qops_ds` for task distribution.
 * @param n The number of worker threads to create.
 * @return A pointer to the newly created worker pool, or NULL if allocation fails.
 */
struct qops_wp *
qops_wp_new(struct qops_ds *q, size_t n);

/**
 * @brief Creates a new worker pool with custom scheduling parameters.
 *
 * This function creates a new worker pool similar to qops_wp_new(), but allows specifying
 * a custom thread scheduling policy and priority for each worker.
 *
 * Supported scheduling policies:
 * - QOPS_SCHED_OTHER (default time-sharing policy)
 * - QOPS_SCHED_RR (real-time round-robin)
 * - QOPS_SCHED_FIFO (real-time first-in-first-out)
 *
 * @param q A pointer to the `qops_ds` for task distribution.
 * @param n The number of worker threads to create.
 * @param sched The scheduling policy to use for worker threads. Use one of the QOPS_SCHED_* macros.
 * @param priority The thread priority (used with real-time policies such as RR or FIFO). Ignored for SCHED_OTHER.
 * @return A pointer to the newly created worker pool, or NULL if allocation or thread setup fails.
 */
struct qops_wp *
qops_wp_new_sched(struct qops_ds *q, size_t n, int sched, int priority);

/**
 * @brief Gets the index of the currently executing worker thread.
 *
 * Returns a thread-local index that uniquely identifies the calling thread
 * within its worker pool. This is useful for assigning per-thread data or tasks.
 *
 * @return The index of the current worker thread, or -1 if called from a non-worker thread.
 */
int
qops_wp_get_local_index(void);

/**
 * @brief Get the total number of worker threads in the pool.
 *
 * Reads the atomic counter that tracks how many worker threads are currently
 * registered in the pool. The value decreases as workers exit. This does not
 * distinguish between active and idle workers; it returns the total count.
 *
 * Thread-safety: lock-free (atomic read).
 *
 * @param pool Pointer to the worker pool instance.
 * @return Total number of worker threads. Returns 0 if @p pool is NULL.
 */
size_t
qops_wp_nof_workers(struct qops_wp *pool);

/**
 * @brief Get the current number of idle worker threads in the pool.
 *
 * Returns the number of workers that are currently idle (i.e., waiting on the
 * pool's condition variable because the buffer is empty). The value is read from
 * an atomic counter and represents a moment-in-time snapshot; it may change
 * immediately under contention.
 *
 * Thread-safety: lock-free (atomic read).
 *
 * @param pool Pointer to the worker pool instance.
 * @return Number of idle workers. Returns 0 if @p pool is NULL.
 */
size_t
qops_wp_nof_idle_workers(struct qops_wp *pool);

/**
 * @brief Execute all tasks from the buffer in the worker pool.
 *
 * Blocks until all tasks in the provided buffer are consumed by workers.
 *
 * @param pool Worker pool context.
 * @param buff Buffer holding tasks to be executed.
 * @return 0 if all tasks executed, -1 if the pool is busy or on error.
 */
int
qops_wp_exec(struct qops_wp *pool, struct qops_ds *buff);


#ifdef __cplusplus
}
#endif

#endif
