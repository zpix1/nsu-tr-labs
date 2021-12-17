#ifndef LAB33_WRITEENTRY_H
#define LAB33_WRITEENTRY_H

enum class AfterWriteAction {
    CLEAN,
    READ_FROM_RESOURCE,
    PAUSE_FD
};

struct WriteEntry {
    std::shared_ptr<std::vector<char>> data;
    size_t written;
    AfterWriteAction action;
    std::shared_ptr<CacheEntry> entry;
};

#endif //LAB33_WRITEENTRY_H
