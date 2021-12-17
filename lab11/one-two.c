#include <stdio.h>
#include <pthread.h>

pthread_mutex_t mutex1;

volatile int who = 1;

void* child(void* data) {
    char* of = data;
    for (int i = 0; i < 10; i++) {
        pthread_mutex_lock(&mutex1);
        if (who == 0) {
            printf("%s\n", of);
            who = 1;
        } else {
            i--;
        }
        pthread_mutex_unlock(&mutex1);
    }
    return NULL;
}

void* parent(void* data) {
    char* of = data;
    for (int i = 0; i < 10; i++) {
        pthread_mutex_lock(&mutex1);
        if (who == 1) {
            printf("%s\n", of);
            who = 0;
        } else {
            i--;
        }
        pthread_mutex_unlock(&mutex1);
    }
    return NULL;
}

int init_m(pthread_mutex_t* mutex_ptr, int prop) {
    pthread_mutexattr_t attrs;
    pthread_mutexattr_init(&attrs);
    pthread_mutexattr_settype(&attrs, prop);
    return pthread_mutex_init(mutex_ptr, &attrs);
}

int main() {
    init_m(&mutex1, PTHREAD_MUTEX_ERRORCHECK);

    pthread_t thread;
    if (pthread_create(&thread, NULL, child, "i am a child thread") != 0) {
        perror("error creating thread");
        return 1;
    }
    parent("i am a parent");

    pthread_exit(NULL);
}
