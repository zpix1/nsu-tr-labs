#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define N 3

pthread_mutex_t mutexes[] = {
        PTHREAD_MUTEX_INITIALIZER,
        PTHREAD_MUTEX_INITIALIZER,
        PTHREAD_MUTEX_INITIALIZER,
};

struct thread_data {
    char *print_it;
    int start_with;
};

void *worker(void *ptr) {
    struct thread_data *data = ptr;

    int current = data->start_with;

    pthread_mutex_t *old;
    pthread_mutex_t *next;

    pthread_mutex_lock(&mutexes[data->start_with]);

    usleep(1000);

    for (int i = 0; i < 10; i++) {
        old = &mutexes[(current + N - 1) % N];
        next = &mutexes[(current + 1) % N];

        if (i != 0) {
            pthread_mutex_unlock(old);
        }

        pthread_mutex_lock(next);

        printf("%s\n", data->print_it);

        current++;
    }

    pthread_mutex_unlock(&mutexes[(current + N - 1) % N]);
    pthread_mutex_unlock(next);

    return NULL;
}

int main() {
    struct thread_data parent_data = {
            "parent",
            1
    };

    struct thread_data child_data = {
            "child",
            0
    };

    pthread_t thread;
    if (pthread_create(&thread, NULL, worker, &child_data) != 0) {
        perror("error creating thread");
        return 1;
    }

    worker(&parent_data);

    pthread_exit(NULL);
}
