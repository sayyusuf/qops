
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

#define BSZ 1024
#define WSZ 16

_Atomic int erc = 0;
_Atomic int inc = 0;

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
main()
{
	struct threadsafeq	*q;
	struct workerp		*p;
	size_t			i;

	if (getuid())
	{
		fprintf(stderr, "Run as root\n");
		return (2);
	}
	q = threadsafeq_new(BSZ);
	p = workerp_new_sched(q, WSZ, WORKERP_SCHED_RR, 99);
	i = 0;
	while (i < LOOP)
	{
		struct qnode node = (struct qnode){.func = func, .cleanup = cleanup, .err = error, .data = DATA};
		workerp_append(p, &node);
		i++;
	}
	while (!workerp_is_idle(p, 100))
		;
	workerp_delete(p);
	threadsafeq_delete(q);
	if (atomic_load(&inc) != LOOP || atomic_load(&erc))
	{
		fprintf(stderr, "Error: LOOP = %d, inc = %d, erc = %d\n", LOOP, atomic_load(&inc), atomic_load(&erc));
		return (1);
	}
	fprintf(stdout, "LOG: LOOP = %d, inc = %d, erc = %d\n", LOOP, atomic_load(&inc), atomic_load(&erc));
	return (0);
}
