# Threadsafe Queue and Worker Pool

This project provides a thread-safe queue implementation along with a worker pool system to process tasks concurrently. It uses POSIX threads (`pthread`) and is designed to be simple yet effective for multi-threaded task execution.

## Overview

- **Threadsafe Queue**: A queue that supports thread-safe operations for appending and removing tasks.
- **Worker Pool**: A pool of worker threads that process tasks from the queue.

## Features

- Queue operations like append and remove are thread-safe.
- Workers in the pool execute tasks concurrently.
- Callback functions for appending tasks and broadcasting signals.
- Graceful shutdown of workers and queues.

## Components

### 1. `Threadsafe Queue`

This queue supports the following operations:

- **Append**: Add a node to the queue.
- **Remove**: Remove a node from the queue.
- **Size**: Get the number of tasks in the queue.
- **Broadcast**: Notify all workers.
- **Destroy**: Clean up resources and free memory.

### 2. `Worker Pool`

This component handles the worker threads that will process tasks concurrently. The worker pool supports:

- **Append tasks**: Add tasks to the pool for processing.
- **Broadcast**: Broadcast a signal to wake all idle workers.
- **Graceful Shutdown**: Request workers to finish their tasks and clean up resources.

---

## Usage Example

### 1. **Creating a Thread-safe Queue and Worker Pool**

First, create a thread-safe queue to hold your tasks:

```c
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
		return 1; // EXIT_FAILURE
	size_t worker_sz = 10; // Number of worker thread
	struct workerp *pool = workerp_new(queue, worker_sz);
	if (!pool)
		return 1; // EXIT_FAILURE
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
	return 0; // EXIT_SUCCESS
}
```
