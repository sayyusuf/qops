
#include <stddef.h>
#include <qops.h>
#include <unistd.h>
#include <string.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define DATA	"Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum."
#define LOOP	1000000
#define LOOP2	1000

#define BSZ 1024
#define WSZ 16

_Atomic int erc = 0;
_Atomic int inc = 0;

int
func(void *data)
{
	volatile float	i;
	volatile float	k;

	i = 0;
	k = 1;
	if (!data || strcmp(DATA, (char *)data))
		return (-1);
	while (i < LOOP2)
		i = i / k + 1;
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
	free(data);
}

char *
ft_strdup(const char *s)
{
	char	*ch;
	int		i;

	i = 0;
	ch = (char *)malloc(strlen(s) + 1);
	if (!ch)
		return (NULL);
	while (s[i] != '\0')
	{
		ch[i] = s[i];
		i++;
	}
	ch[i] = '\0';
	return (ch);
}

int
main()
{
	struct threadsafeq	*q;
	struct workerp		*p;
	size_t			i;

	q = threadsafeq_new(BSZ);
	p = workerp_new(q, WSZ);
	i = 0;
	while (i < LOOP)
	{
		char *str = ft_strdup(DATA);
		struct qnode node = (struct qnode){.func = func, .cleanup = cleanup, .err = error, .data = str};
		workerp_append_quiet(p, &node);
		i++;
	}
	workerp_broadcast(p);
	while (workerp_delete(p))
		;
	int ret = !threadsafeq_size(q);
	threadsafeq_delete(q);
	return (ret);
}
