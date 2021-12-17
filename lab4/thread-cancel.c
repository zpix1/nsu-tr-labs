#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

void* print_inf(void* data) {
    char* of = (char*) data;
#ifdef v2
    pthread_cleanup_push(printf, "the end\n");
#endif

    for (;;) {
        printf("%s\n", of);
        pthread_testcancel();
    }

#ifdef v2
    pthread_cleanup_pop(1);
#endif

    return NULL;
}

int main() {
    pthread_t thread;
    if (pthread_create(&thread, NULL, print_inf, "i am a child thread") != 0) {
        perror("error creating thread");
        return 1;
    }

    sleep(2);

    if (pthread_cancel(thread) != 0) {
        perror("error cancelling thread");
        return 1;
    }
    
    pthread_exit(NULL);
}
