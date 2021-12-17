#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

void printf_d(void* d) {
    printf("counted up to %d", *(int*)d);
}

void* count_to(void* of) {
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    //pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    int a = 0;
    pthread_cleanup_push(printf_d, &a);

    for (a = 0; a < 100000000; a++);

    pthread_cleanup_pop(1);

    return NULL;
}

int main() {
    pthread_t thread;
    if (pthread_create(&thread, NULL, count_to, NULL) != 0) {
        perror("error creating thread");
        return 1;
    }

    for (int i = 0; i < 1000000; i++);

    if (pthread_cancel(thread) != 0) {
        perror("error cancelling thread");
        return 1;
    }

    pthread_exit(NULL);
}
