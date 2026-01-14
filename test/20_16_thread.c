
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
	struct qops_buff	*b;
	struct qops_pool	*p;
	size_t			i;

	p = qops_pool_new(NULL, WSZ);
	b = qops_buff_new(LOOP, QOPS_STATIC_BUFF);
	i = 0;
	while (i < LOOP)
	{
		struct qops_node node = (struct qops_node){.func = func, .cleanup = cleanup, .err = error, .data = DATA};
		qops_buff_write(b, &node);
		i++;
	}
	if (qops_pool_exec(p, b) != 0)
	{
		fprintf(stderr, "Error: qops_pool_exec failed\n");
		qops_buff_delete(b);
		qops_pool_delete(p);
		return (1);
	}
	qops_buff_delete(b);
	qops_pool_delete(p);
	if (atomic_load(&inc) != LOOP || atomic_load(&erc))
	{
		fprintf(stderr, "Error: LOOP = %d, inc = %d, erc = %d\n", LOOP, atomic_load(&inc), atomic_load(&erc));
		return (1);
	}
	fprintf(stdout, "LOG: LOOP = %d, inc = %d, erc = %d\n", LOOP, atomic_load(&inc), atomic_load(&erc));
	return (0);
}
