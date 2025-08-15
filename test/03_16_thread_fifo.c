
#include <stddef.h>
#include <qops.h>
#include <unistd.h>

#define LOOP 10000000
#define LOOP2 1000

int
func(void *data)
{
	(void)data;
	volatile float	i;
	volatile float	k;

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
	p = workerp_new_sched(q, 16, WORKERP_SCHED_FIFO, 99);
	if (!p)
		return (1);
	i = 0;
	while (i < LOOP)
	{
		struct qnode node = (struct qnode){.func = func, .cleanup = NULL, .err = NULL, .data = NULL};
		workerp_append(p, &node);
		i++;
	}
	while (!workerp_is_idle(p, 100))
		;
	workerp_delete(p);
	threadsafeq_delete(q);
	return (0);
}
