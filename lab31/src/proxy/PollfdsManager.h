#ifndef LAB33_POLLFDSMANAGER_H
#define LAB33_POLLFDSMANAGER_H

#include <vector>
#include <sys/poll.h>

class PollfdsManager {
    std::vector<struct pollfd> poll_entries;

public:
    void add_fd(int fd, short wait_for = POLLIN) {
        struct pollfd entry{
                .fd=fd,
                .events=wait_for,
                .revents=0
        };
        poll_entries.push_back(entry);
    }

    void switch_fd(int fd, short wait_for) {
        auto position = std::find_if(poll_entries.begin(), poll_entries.end(),
                                     [&fd](const auto& value) { return value.fd == fd; });
        position->events = wait_for;
    }

    std::vector<int> make_poll() {
        auto rc = poll(poll_entries.data(), poll_entries.size(), -1);

        if (rc < 0) {
            throw_errno("failed to poll()");
        }

        std::vector<int> readable_fds;
        for (const auto& entry: poll_entries) {
            if (entry.revents == 0) {
                continue;
            }

//            if (entry.revents != POLLIN) {
//                throw std::runtime_error("unexpected non-POLLIN");
//            }

            readable_fds.push_back(entry.fd);
        }

        return readable_fds;
    }

    void remove_fd(int fd) {
        auto position = std::find_if(poll_entries.begin(), poll_entries.end(),
                                     [&fd](const auto& value) { return value.fd == fd; });
        if (position != poll_entries.end()) {
            poll_entries.erase(position);
        }
    }
};

#endif //LAB33_POLLFDSMANAGER_H
