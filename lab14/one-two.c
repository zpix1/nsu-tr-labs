#include <stdio.h>
#include <pthread.h>
#include "sem_polyfill.h"

#define REPEAT 10
#define N 1

struct rk_sema filled_count;
struct rk_sema empty_count;

void *parent(void *ptr) {
    char *of = ptr;

    for (int i = 0; i < REPEAT; i++) {
        rk_sema_wait(&empty_count);
        printf("%s\n", of);
        rk_sema_post(&filled_count);
    }

    return NULL;
}

void *child(void *ptr) {
    char *of = ptr;

    for (int i = 0; i < REPEAT; i++) {
        rk_sema_wait(&filled_count);
        printf("%s\n", of);
        rk_sema_post(&empty_count);
    }

    return NULL;
}

int main() {
    rk_sema_init(&filled_count, 0);
    rk_sema_init(&empty_count, N);

    pthread_t thread;
    if (pthread_create(&thread, NULL, child, "child") != 0) {
        perror("error creating thread");
        return 1;
    }

    parent("parent");

    pthread_exit(NULL);
}
