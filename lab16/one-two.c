#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <semaphore.h>
#include <stdlib.h>

sem_t *filled_count = SEM_FAILED;
#define filled_name "/asdfiuja"
sem_t *empty_count = SEM_FAILED;
#define empty_name "/123asdfasdf42"

#define DO(x, s) do { \
        if ((x) < 0) { \
                    perror(s); \
                } \
} while (0)

#define REPEAT 10

void clear_all() {
    if (filled_count != SEM_FAILED) DO(sem_close(filled_count), filled_name " close");
    if (empty_count != SEM_FAILED) DO(sem_close(empty_count), empty_name " close");
}

void unlink_all() {
    if (filled_count != SEM_FAILED) DO(sem_unlink(filled_name), filled_name " unlink");
    if (empty_count != SEM_FAILED) DO(sem_unlink(empty_name), empty_name " unlink");
}

int main() {
    DO(filled_count = sem_open(filled_name, O_CREAT, 0777, 1), filled_name" open");
    DO(empty_count = sem_open(empty_name, O_CREAT, 0777, 0), empty_name" open");

    printf("%p %p\n", filled_count, empty_count);

    if (filled_count == SEM_FAILED || empty_count == SEM_FAILED) {
        perror("failed to create sems");
        clear_all();
        unlink_all();
        exit(1);
    }

    if (fork() > 0) {
        int i;
        for (i = 0; i < REPEAT; i++) {
            if (sem_wait(filled_count)) break;
            printf("parent\n");
            if (sem_post(empty_count)) break;
        }

        clear_all();
        unlink_all();

        if (i != REPEAT) {
            exit(1);
        }

        wait(NULL);

        exit(0);
    } else {
        int i;
        for (i = 0; i < REPEAT; i++) {
            if (sem_wait(empty_count)) break;
            printf("child\n");
            if (sem_post(filled_count)) break;
        }
        clear_all();
        if (i != REPEAT) {
            exit(1);
        }
        exit(0);
    }
}
