
#include <stddef.h>
#include <qops.h>
#include <unistd.h>

#define LOOP 10000000
#define LOOP2 1000

int func(void *data)
{
	(void)data;
	float	i;
	float	k;

	i = 0;
	k = 1;
	while (i < LOOP2)
		i = i / k + 1;
	return (0);
}

int main()
{
	struct threadsafeq	*q;
	struct workerp		*p;
	size_t			i;

	q = threadsafeq_new(0);
	p = workerp_new_sched(q, 16, WORKERP_SCHED_RR, 99);
	if (!p)
		return (1);
	i = 0;
	while (i < LOOP)
	{
		struct qnode node = (struct qnode){.func = func, .cleanup = NULL, .err = NULL, .data = NULL};
		workerp_append_quiet(p, &node);
		i++;
	}
	workerp_broadcast(p);
	while (!workerp_is_idle(p, 100))
		;
	workerp_destroy(p);
	threadsafeq_destroy(q);
	return (0);
}
