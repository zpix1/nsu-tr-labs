#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <math.h>
#include <signal.h>

#define CHECK_STEP 10000000

struct calc_part {
    double result;
    int start;
    int step;
};

volatile sig_atomic_t stop = 0;
short exit_next = 0;
pthread_mutex_t stop_mutex;
static pthread_barrier_t barrier;

void* stop_manager(void* data) {
    sigset_t* set = data;
    int sig, s;
    for (;;) {
        sig = sigwait(set);

        printf("Signal handling thread got signal %d\n", sig);
        if (sig == SIGINT) {
            pthread_mutex_lock(&stop_mutex);
            stop = 1;
            pthread_mutex_unlock(&stop_mutex);
            return NULL;
        }
    }
}

void* calculate(void* data) {
    struct calc_part* part = (struct calc_part*) data;

    long long check_idx = 0;
    for (long long i = part->start; ; i += part->step) {
        part->result += 1.0/(i*4.0 + 1.0);
        part->result -= 1.0/(i*4.0 + 3.0);

        check_idx++;
        if (check_idx % CHECK_STEP == 0) {
            pthread_mutex_lock(&stop_mutex);
            exit_next = stop;
            pthread_mutex_unlock(&stop_mutex);
            pthread_barrier_wait(&barrier);
            if (exit_next) {
                break;
            }
        }
    }
    part->result *= 4;

    printf("[%d]: counted up to %lld\n", part->start, check_idx * part->step + part->start);

    pthread_exit((void*) part);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s [num of threads]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int num_of_threads = atoi(argv[1]);
    struct calc_part calc_parts[num_of_threads];
    pthread_t threads[num_of_threads];

    pthread_mutex_init(&stop_mutex, NULL);

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) {
        perror("pthread_sigmask");
        exit(EXIT_FAILURE);
    }

    if (pthread_barrier_init(&barrier, NULL, num_of_threads) != 0) {
        printf("can't init barrier\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < num_of_threads; i++) {
        calc_parts[i].result = 0;
        calc_parts[i].start = i;
        calc_parts[i].step = num_of_threads;

        printf("[%d]: from %d with step %d\n", i, calc_parts[i].start, calc_parts[i].step);

        if (pthread_create(&threads[i], NULL, calculate, &calc_parts[i]) != 0) {
            perror("failed to start thread");
            exit(EXIT_FAILURE);
        }
    }

    pthread_t manager;
    if (pthread_create(&manager, NULL, stop_manager, &set) != 0) {
        perror("pthread_create manager");
        exit(EXIT_FAILURE);
    }

    double pi_result = 0;

    pthread_join(manager, NULL);

    for (int i = 0; i < num_of_threads; i++) {
        void* data;
        if (pthread_join(threads[i], &data) != 0) {
            perror("failed to join thread");
            exit(EXIT_FAILURE);
        }
        struct calc_part* part = (struct calc_part*) data;
        pi_result += part->result;
    }

    pthread_barrier_destroy(&barrier);

    printf("calculated pi: %.17g\n", pi_result);
    printf("delta: %.17g\n", fabs(pi_result - M_PI));

    return EXIT_SUCCESS;
}
