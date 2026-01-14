
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
	struct qops_pool	*p;

	p = (struct qops_pool *)data;
	while (atomic_fetch_add(&cnt, 1) < LOOP)
	{
		struct qops_node node = (struct qops_node){.func = func, .cleanup = cleanup, .err = error, .data = DATA};
		qops_pool_write(p, &node);
	}
	return (0);
}

int
main()
{
	struct qops_buff	*q;
	struct qops_pool	*p;
	struct qops_buff	*q2;
	struct qops_pool	*p2;
	size_t			i;

	q = qops_buff_new(BSZ, QOPS_LINKED_BUFF);
	p = qops_pool_new(q, WSZ);
	q2 = qops_buff_new(BSZ, QOPS_LINKED_BUFF);
	p2 = qops_pool_new(q2, WSZ);
	i = 0;
	while (i < WSZ)
	{
		struct qops_node node = (struct qops_node){.func = func2, .cleanup = NULL, .err = NULL, .data = p};
		qops_pool_write(p2, &node);
		i++;
	}
	while (!qops_pool_is_idle(p2, 100) || !qops_pool_is_idle(p, 100))
		;
	qops_pool_delete(p);
	qops_buff_delete(q);
	qops_pool_delete(p2);
	qops_buff_delete(q2);
	if (atomic_load(&inc) != LOOP || atomic_load(&erc))
	{
		fprintf(stderr, "Error: LOOP = %d, inc = %d, erc = %d\n", LOOP, atomic_load(&inc), atomic_load(&erc));
		return (1);
	}
	fprintf(stdout, "LOG: LOOP = %d, inc = %d, erc = %d\n", LOOP, atomic_load(&inc), atomic_load(&erc));
	return (0);
}
