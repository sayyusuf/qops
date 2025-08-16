
#include <stddef.h>
#include <qops.h>
#include <unistd.h>

#define LOOP 10000000
#define LOOP2 1000

_Atomic int inc = 0;

int func(void *data)
{
	(void)data;
	float	i;
	float	k;

	i = 0;
	k = 1;
	while (i < LOOP2)
		i = i / k + 1;
	++inc;
	return (0);
}

int main()
{
	struct threadsafeq	*q;
	struct workerp		*p;
	size_t			i;
	int			ret;

	ret = 0;
	q = threadsafeq_new(0);
	p = workerp_new(q, 16);
	i = 0;
	while (i < LOOP)
	{
		struct qnode node = (struct qnode){.func = func, .cleanup = NULL, .err = NULL, .data = NULL};
		workerp_append_quiet(p, &node);
		i++;
	}
	workerp_broadcast(p);
	while (workerp_delete(p))
		;
	ret = !threadsafeq_size(q);
	threadsafeq_delete(q);
	return (ret);
}
