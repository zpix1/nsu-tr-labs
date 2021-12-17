#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#define REPEAT 10
#define N 2

pthread_cond_t cv;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

volatile int who = 0;

struct setup {
    char *of;
    int who_am_i;
};

void *worker(void *ptr) {
    char *of = ((struct setup *) ptr)->of;
    int who_am_i = ((struct setup *) ptr)->who_am_i;

    for (int i = 0; i < REPEAT; i++) {
        pthread_mutex_lock(&mutex);
        if (i != 0 || who_am_i != who) {
            do {
                pthread_cond_wait(&cv, &mutex);
            } while (who != who_am_i);
        }
        printf("%s\n", of);
        who = (who + 1) % N;
        pthread_mutex_unlock(&mutex);
        pthread_cond_signal(&cv);
    }

    return NULL;
}

int main() {
    pthread_cond_init(&cv, NULL);

    struct setup parent = {
            .of="parent",
            .who_am_i=0
    };

    struct setup child = {
            .of="child",
            .who_am_i=1
    };


    pthread_t thread;
    if (pthread_create(&thread, NULL, worker, &child) != 0) {
        perror("error creating thread");
        return 1;
    }

    worker(&parent);

    pthread_exit(NULL);
}
