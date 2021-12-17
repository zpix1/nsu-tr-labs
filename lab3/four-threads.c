#include <stdio.h>
#include <pthread.h>

void* print_lines(char** lines) {
    for (char** set = lines; *set; set++) {
        printf("%s\n", *set);
    }
    return NULL;
}

int main() {
    pthread_t threads[4];

    char* messages[4][4] = {
        {"1 first", "1 thread", "1 here", NULL},
        {"2 second", "2 thread", "2 here", NULL},
        {"3 third", "3 thread", "3 here", NULL},
        {"4 last", "4 thread", "4 here", NULL}
    };

    for (int i = 0; i < 4; i++) {
        if (pthread_create(&threads[i], NULL, print_lines, messages[i]) != 0) {
            perror("error creating thread");
            return 1;
        }
    }

    for (int i = 0; i < 4; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            perror("error joining thread");
            return 1;
        }
    }

    return 0;
}