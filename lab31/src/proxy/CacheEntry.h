#ifndef LAB33_CACHE_H
#define LAB33_CACHE_H

#include <unordered_set>

class CacheEntry {
public:
    http_parser* parser;
    std::unordered_set<int> waiting_sockets;

    std::string key;

    std::shared_ptr<std::vector<char>> data;

    bool done = false;
};

#endif //LAB33_CACHE_H
