#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <math.h>

#define NUM_STEP 200000000

struct calc_part {
    double result;
    int start;
    int step;
};

void* calculate(void* data) {
    struct calc_part* part = (struct calc_part*) data;
    int i;
    for (i = part->start; ; i += part->step) {
        part->result += 1.0/(i*4.0 + 1.0);
        part->result -= 1.0/(i*4.0 + 3.0);

        if (i >= NUM_STEP) {
            break;
        }
    }
    part->result *= 4;

    printf("[%d]: counted up to %d\n", part->start, i);

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

    double pi_result = 0;

    for (int i = 0; i < num_of_threads; i++) {
        void* data;
        if (pthread_join(threads[i], &data) != 0) {
            perror("failed to join thread");
            exit(EXIT_FAILURE);
        }
        struct calc_part* part = (struct calc_part*) data;
        pi_result += part->result;
    }

    printf("calculated pi: %.17g\n", pi_result);
    printf("delta: %.17g\n", fabs(pi_result - M_PI));

    return EXIT_SUCCESS;
}