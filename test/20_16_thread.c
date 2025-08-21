
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
	struct qbuff	*b;
	struct workerp	*p;
	size_t			i;

	p = workerp_new(NULL, WSZ);
	b = qbuff_new(LOOP);
	i = 0;
	while (i < LOOP)
	{
		qbuff_write(b, DATA, func, error, cleanup);
		i++;
	}
	workerp_exec(p, b);
	qbuff_delete(b);
	workerp_delete(p);
	if (atomic_load(&inc) != LOOP || atomic_load(&erc))
	{
		fprintf(stderr, "Error: LOOP = %d, inc = %d, erc = %d\n", LOOP, atomic_load(&inc), atomic_load(&erc));
		return (1);
	}
	fprintf(stdout, "LOG: LOOP = %d, inc = %d, erc = %d\n", LOOP, atomic_load(&inc), atomic_load(&erc));
	return (0);
}
