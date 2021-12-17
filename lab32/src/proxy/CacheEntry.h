#ifndef LAB33_CACHE_H
#define LAB33_CACHE_H

#include <unordered_set>
#include <utility>
#include <vector>

#include <pthread.h>

#include "pthread_helpers.h"

class CacheEntry {
public:
    enum class State {
        LOADING,
        SUCCESS,
        FAILED
    };

    State state = State::LOADING;
    std::string key;
    std::vector<char> data;

    LockWrapper lock;
    CVWrapper data_change_cv;

    AtomicInt listeners_count;
};

#endif //LAB33_CACHE_H
