#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <time.h>
#include <qops.h>
#include <sys/time.h>

#define NUM_TASKS 1000000
#define NUM_WORKERS 16  // Donanımınıza göre ayarlayabilirsiniz

struct my_data {
    atomic_size_t* counter;
};

int increment_func(void* data) {
    struct my_data* d = (struct my_data*)data;
    atomic_fetch_add(d->counter, 1);
    return 0;
}

int main(void) {
    atomic_size_t counter = 0;
    struct qbuff* b = qbuff_new(NUM_TASKS, increment_func, NULL, NULL);
    struct qops_* pool = qops_new(NULL, NUM_WORKERS);
    struct my_data* datas = malloc(NUM_TASKS * sizeof(struct my_data));
    size_t i;
    clock_t start, end;
    struct timeval s, e;
    long seconds, useconds;
    long total_microseconds;


    start = clock();
    gettimeofday(&s, NULL);
    for (i = 0; i < NUM_TASKS; ++i) {
        datas[i].counter = &counter;
	qbuff_write(b, &datas[i]);
    }
    qops_exec(pool, b);
    gettimeofday(&e, NULL);
    end = clock();

    qbuff_delete(b);
    qops_delete(pool);
    free(datas);
    seconds  = e.tv_sec  - s.tv_sec;
    useconds = e.tv_usec - s.tv_usec;

    total_microseconds = (seconds * 1000000) + useconds;

    printf("Geçen süre: %ld mikro saniye\n", total_microseconds);

    printf("QOPS C Test: Sayaç = %zu, Süre = %.2f ms\n", (size_t)counter,
           1000.0 * (end - start) / CLOCKS_PER_SEC);

    return 0;
}
