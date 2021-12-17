#include <stdio.h>
#include <pthread.h>

void* print_lines(char* of) {
    for (int i = 0; i < 10; i++) {
        printf("%s\n", of);
    }
    return NULL;
}

int main() {
    pthread_t thread;
    if (pthread_create(&thread, NULL, print_lines, "i am a child thread") != 0) {
        perror("error creating thread");
        return 1;
    }
    
#ifdef v2
    if (pthread_join(thread, NULL) != 0) {
        perror("error joining thread");
        return 1;
    }
#endif

    print_lines("i am a parent");

#ifdef join
    if (pthread_join(thread, NULL) != 0) {
        perror("error joining thread");
        return 1;
    }
#elif defined(exit)
    pthread_exit(NULL);
#else
    return 0;
#endif
}
