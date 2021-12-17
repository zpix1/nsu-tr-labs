#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

static pthread_barrier_t barrier;

void* str_sleep(char* str) {
    int len = strlen(str);
    pthread_barrier_wait(&barrier);
    usleep(len*10000);
    printf("%s\n", str);
    return NULL;
}

int main() {
    pthread_t threads[100];
    char strings[100][101];

    int total = 0;
    for (int i = 0; i < 100; i++) {
        if (!fgets(strings[i], 100, stdin)) {
            total = i;
            break;
        }
        strings[i][strcspn(strings[i], "\n")] = 0;
    }

    if (pthread_barrier_init(&barrier, NULL, total) != 0) {
        printf("can't init barrier\n");
        return 1;
    }


    for (int i = 0; i < total; i++) {
        if (pthread_create(&threads[i], NULL, str_sleep, strings[i]) != 0) {
            perror("error creating thread");
            return 1;
        }
    }

    for (int i = 0; i < total; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            perror("error joining thread");
            return 1;
        }
    }

    pthread_barrier_destroy(&barrier);

    pthread_exit(NULL);
}
