#include <stddef.h>
#include <qops.h>
#include <unistd.h>
#include <string.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define DATA	"Lorem ipsum"
#define LOOP	10000000
#define LOOP2	1000

#define BSZ 0
#define WSZ 8

_Atomic int erc = 0;
_Atomic int inc = 0;
_Atomic int cnt = 0;

int
func(void *data)
{
	if (!data || strcmp(DATA, (char *)data))
		return (-1);
	atomic_fetch_add(&inc, 1);
	return (0);
}


void
error(void *data, int errorcode)
{
	(void)data;
	(void)errorcode;
	atomic_fetch_add(&erc, 1);
}

void
cleanup(void *data)
{
	(void)data;
}

int
func2(void *data)
{
	struct workerp		*p;

	p = (struct workerp *)data;
	while (atomic_fetch_add(&cnt, 1) < LOOP)
	{
		struct qnode node = (struct qnode){.func = func, .cleanup = cleanup, .err = error, .data = DATA};
		workerp_append(p, &node);
	}
	return (0);
}

int
main()
{
	struct threadsafeq	*q;
	struct workerp		*p;
	struct threadsafeq	*q2;
	struct workerp		*p2;
	size_t			i;

	if (getuid())
	{
		fprintf(stderr, "Run as root\n");
		return (2);
	}
	q = threadsafeq_new(BSZ);
	p = workerp_new_sched(q, WSZ, WORKERP_SCHED_FIFO, 99);
	q2 = threadsafeq_new(BSZ);
	p2 = workerp_new(q2, WSZ);
	i = 0;
	while (i < WSZ)
	{
		struct qnode node = (struct qnode){.func = func2, .cleanup = NULL, .err = NULL, .data = p};
		workerp_append(p2, &node);
		i++;
	}
	while (!workerp_is_idle(p, 100) || !workerp_is_idle(p2, 100))
		;
	workerp_delete(p);
	threadsafeq_delete(q);
	workerp_delete(p2);
	threadsafeq_delete(q2);
	if (atomic_load(&inc) != LOOP || atomic_load(&erc))
	{
		fprintf(stderr, "Error: LOOP = %d, inc = %d, erc = %d\n", LOOP, atomic_load(&inc), atomic_load(&erc));
		return (1);
	}
	fprintf(stdout, "LOG: LOOP = %d, inc = %d, erc = %d\n", LOOP, atomic_load(&inc), atomic_load(&erc));
	return (0);
}