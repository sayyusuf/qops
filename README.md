# Threadsafe buffer and Thread Pool

This project provides a thread-safe buffer implementation along with a worker pool system to process tasks concurrently. It uses POSIX threads (`pthread`) and is designed to be simple yet effective for multi-threaded task execution.

## Overview

- **Threadsafe buffer**: A buffer that supports thread-safe operations for writeing and removing tasks.
- **Worker Pool**: A pool of worker threads that process tasks from the buffer.

## Features

- buffer operations like write and read are thread-safe.
- Workers in the pool execute tasks concurrently.
- Callback functions for writeing tasks and broadcasting signals.
- Graceful shutdown of workers and buffers.

## Components

### 1. `Threadsafe buffer`

This buffer supports the following operations:

- **write**: Add a node to the buffer.
- **read**: read a node from the buffer.
- **Size**: Get the number of tasks in the buffer.
- **Broadcast**: Notify all workers.
- **Delete**: Clean up resources and free memory.

### 2. `Worker Pool`

This component handles the worker threads that will process tasks concurrently. The worker pool supports:

- **write tasks**: Add tasks to the pool for processing.
- **Broadcast**: Broadcast a signal to wake all idle workers.
- **Graceful Shutdown**: Request workers to finish their tasks and clean up resources.

---

## Usage Example

### 1. **Creating a Thread-safe buffer and Worker Pool**

First, create a thread-safe buffer to hold your tasks:

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
	struct qops_buff *buffer = qops_buff_new(buff_sz);
	if (!buffer)
		return 1; // EXIT_FAILURE
	size_t worker_sz = 10; // Number of worker thread
	struct qops_ *pool = qops_new(buffer, worker_sz);
	if (!pool)
		return 1; // EXIT_FAILURE
	struct qops_node node1 = (struct qops_node){.data = "hello", .func = func, .cleanup = cleanup, .err = error};
	struct qops_node node2 = (struct qops_node){.data = "world", .func = func, .cleanup = cleanup, .err = error};
	qops_pool_write(pool, &node1);
	while (!qops_is_idle(pool, 100)) // Wait 100 ms to finish all tasks in the loop
		;
	qops_delete(pool);
	qops_buff_delete(buffer);
	return 0; // EXIT_SUCCESS
}
```
