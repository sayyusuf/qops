#include <qops.h>
#include <stdio.h>

int func(void *data)
{
	printf("%s\n", (char *)data);
	return 0; // If the return value is not 0, the error function is called
}

void cleanup(void *data)
{
	(void)data;
}

void error(void *data, int errno)
{
	(void)data;
	(void)errno;
}

int main()
{
	size_t buff_sz = 10; // Number of nodes per buffer
	struct threadsafeq *queue = threadsafeq_new(buff_sz);
	if (!queue)
		return 1;
	size_t worker_sz = 10; // Number of worker thread
	struct workerp *pool = workerp_new(queue, worker_sz);
	if (!pool)
		return 1;

	struct qnode node1 = (struct qnode){.data = "hello", .func = func, .cleanup = cleanup, .err = error};
	struct qnode node2 = (struct qnode){.data = "world", .func = func, .cleanup = cleanup, .err = error};
	// There are two methods to append
	// 1. Using pool
	workerp_append(pool, &node1);
	// 2. Using queue
	threadsafeq_append(queue, &node2);
	while (!workerp_is_idle(pool, 100)) // Wait 100 ms to finish all tasks in the loop
		;
	workerp_destroy(pool);
	threadsafeq_destroy(queue);
	return 0;
}