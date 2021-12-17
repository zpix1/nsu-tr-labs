#ifndef LAB32_PROXY_H
#define LAB32_PROXY_H

#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <csignal>
#include <chrono>

#include <unordered_map>
#include <sstream>
#include <netdb.h>
#include <memory>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>

#include <pthread.h>

#include <plog/Log.h>
#include <plog/Init.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Appenders/ColorConsoleAppender.h>

#include <http_parser.h>
#include <plog/Appenders/RollingFileAppender.h>

#include "http_callbacks.h"
#include "utils.h"
#include "pthread_helpers.h"
#include "CacheEntry.h"

#define HTTP_NEWLINE "\r\n"

namespace proxy {
    const int ON = 1;
    const size_t BUF_SIZE = (1 << 20);
    const int BACKLOG = 300;

    class Proxy {
    private:

        int listen_port;
        int listen_socket_fd = -1;

        struct ThreadData {
            Proxy *proxy;
            int fd;
        };

        LockWrapper cache_lock;
        std::unordered_map<std::string, std::shared_ptr<CacheEntry>> cache;
        std::unordered_map<int, std::shared_ptr<CacheEntry>> fd_to_cache;

    public:
        explicit Proxy(int listen_port) : listen_port(listen_port) {};

        void run() {
            static plog::RollingFileAppender<plog::TxtFormatter> fileAppender("log.txt", 8000, 3);
            static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
            plog::init(plog::debug)
                .addAppender(&fileAppender)
                .addAppender(&consoleAppender);

            ::signal(SIGPIPE, SIG_IGN);
            listen_socket_fd = connect_socket(listen_port);
            PLOGI << "Connected listen socket " << listen_socket_fd;

            while (int new_connection_fd = accept(listen_socket_fd, nullptr, nullptr)) {
                if (new_connection_fd <= 0) {
                    throw_errno("accept() failed");
                }
                PLOGI << "New connection " << new_connection_fd << " creating new thread";

                auto arg = new ThreadData{this, new_connection_fd};

                pthread_t thread;

                // it's not good that thread frees its argument, but what can I do?
                auto rc = pthread_create(&thread, nullptr, &Proxy::process_client_connection, arg);
                if (rc != 0) {
                    PLOGI << "Can't create a thread for connection, closing it";
                    close(new_connection_fd);
                } else {
                    rc = pthread_detach(thread);
                    if (rc != 0) {
                        throw_errno("pthread_detach() failed");
                    }
                }
            }

            throw_errno("accept() failed");
        }

        static void *process_client_connection(void *data) {
            auto arg = reinterpret_cast<ThreadData *>(data);
            auto client_fd = arg->fd;
            auto proxy = arg->proxy;
            auto buf = new char[BUF_SIZE];

            try {
                PLOGI << "Processing client connection " << arg->fd;
                custom_data_t parse_data;
                http_parser parser;
                http_parser_init(&parser, HTTP_REQUEST);
                parser.data = &parse_data;

                http_parser_settings settings;
                http_parser_settings_init(&settings);
                settings.on_url = on_url_callback;
                settings.on_header_field = on_header_field;
                settings.on_header_value = on_header_value;
                settings.on_headers_complete = on_headers_complete;

                while (!parse_data.done) {
                    auto read_count = read(client_fd, buf, BUF_SIZE);

                    PLOGD << "Read " << read_count << " from client " << client_fd;

                    if (read_count < 0) {
                        throw_errno("client read() failed");
                    }

                    auto parsed_count = http_parser_execute(&parser, &settings, buf, read_count);

                    if (read_count != parsed_count) {
                        throw_errno("Parse error: parsed " + std::to_string(parsed_count) + " of " +
                                    std::to_string(read_count) + " read");
                    }

                    if (read_count == 0) {
                        break;
                    }
                }

                if (!parse_data.done) {
                    throw_errno("Parse error: invalid request", true);
                }

                if (parser.method != HTTP_GET) {
                    throw_errno("Non GET methods are not supported (got " +
                                std::string(http_method_str((http_method) (parser.method))) + ")", true);
                }

                const auto cache_key = parse_data.headers["host"] + parse_data.url;

                PLOGI << "Parsed client " << client_fd << " with key = " << cache_key;

                proxy->cache_lock.lock();
                auto entry = proxy->cache.find(cache_key);

                // Create cache entry if it does not exist
                if (entry == proxy->cache.end()) {
                    proxy->cache_lock.unlock();
                    int resource_fd = create_resource_request(parse_data.url, parse_data.headers);
                    proxy->cache_lock.lock();

                    PLOGI << "Not found " << cache_key << " in cache";
                    auto cache_entry = std::make_shared<CacheEntry>();
                    cache_entry->key = cache_key;
                    proxy->cache[cache_key] = cache_entry;
                    PLOGI << "url for creating server " << parse_data.url;

                    proxy->fd_to_cache[resource_fd] = cache_entry;

                    auto resource_arg = new ThreadData{proxy, resource_fd};

                    pthread_t thread;

                    auto rc = pthread_create(&thread, nullptr, &Proxy::process_resource_connection, resource_arg);
                    if (rc != 0) {
                        proxy->cache_lock.unlock();
                        close(resource_fd);
                        throw_errno("failed to pthread_create() for resource read");
                    } else {
                        rc = pthread_detach(thread);
                        if (rc != 0) {
                            proxy->cache_lock.unlock();
                            close(resource_fd);
                            throw_errno("pthread_detach() failed");
                        }
                    }
                } else {
                    PLOGI << "Found " << cache_key << " in cache";
                }

                auto cache_entry = proxy->cache[cache_key];

                cache_entry->listeners_count.change(1);

                proxy->cache_lock.unlock();

                size_t already_written = 0;

                cache_entry->lock.lock();
                while (true) {
                    // Lock and copy to buffer
                    // TODO: Use rwlock
                    if (cache_entry->state == CacheEntry::State::FAILED) {
                        cache_entry->lock.unlock();
                        cache_entry->listeners_count.change(-1);
                        throw_errno("failed server", true);
                    }

                    size_t to_write = std::min(cache_entry->data.size() - already_written, BUF_SIZE);
                    std::copy(cache_entry->data.begin() + already_written,
                              cache_entry->data.begin() + already_written + to_write, buf);
                    cache_entry->lock.unlock();

                    PLOGD << "Writing bytes=" << to_write << " fd=" << client_fd;
                    auto written_count = write(client_fd, buf, to_write);

                    if (written_count < 0) {
                        cache_entry->listeners_count.change(-1);
                        throw_errno("Failed to write()");
                    }

                    if (written_count != to_write) {
                        cache_entry->listeners_count.change(-1);
                        throw_errno("Failed to write, written_count != to_write", true);
                    }

                    already_written += written_count;

                    cache_entry->lock.lock();
                    auto is_already_all = already_written == cache_entry->data.size();
                    if (cache_entry->state == CacheEntry::State::SUCCESS) {
                        if (is_already_all) {
                            break;
                        } else {
                            continue;
                        }
                    } else if (cache_entry->state == CacheEntry::State::LOADING) {
                        if (is_already_all) {
                            PLOGD << "Waiting for data on cv fd=" << client_fd;
                            do {
                                cache_entry->data_change_cv.wait(cache_entry->lock);
                                PLOGD << "WAKE UP for cv fd=" << client_fd;
                            } while (cache_entry->state == CacheEntry::State::LOADING &&
                                     already_written == cache_entry->data.size());
                        } else {
                            continue;
                        }
                    }
                }

                cache_entry->listeners_count.change(-1);
                cache_entry->lock.unlock();
            } catch (const std::system_error& exception) {
                PLOGW << "Client processing error: " << exception.what() << " " << client_fd;
                send_http_error(client_fd, 500, exception.what());
            }

            delete[] buf;
            delete arg;
            close(client_fd);

            return nullptr;
        }

        static void *process_resource_connection(void *data) {
            auto arg = reinterpret_cast<ThreadData *>(data);
            auto resource_fd = arg->fd;
            auto proxy = arg->proxy;

            proxy->cache_lock.lock();
            auto cache_entry = proxy->fd_to_cache[resource_fd];
            PLOGI << "started reading from " << cache_entry->key;
            proxy->cache_lock.unlock();


            custom_data_t parse_data;
            http_parser parser;
            http_parser_init(&parser, HTTP_RESPONSE);
            parser.data = &parse_data;

            http_parser_settings settings;
            http_parser_settings_init(&settings);
            settings.on_message_complete = on_message_complete;

            auto buf = new char[BUF_SIZE];

            while (true) {
                try {
                    auto read_count = read(resource_fd, buf, BUF_SIZE);
                    PLOGD << "read from resource " << read_count << cache_entry->key;

                    if (read_count < 0) {
                        throw_errno("failed to read() resource reply");
                    }

                    auto parsed_count = http_parser_execute(&parser, &settings, buf, read_count);

                    if (parsed_count != read_count) {
                        throw_errno("failed to parse resource reply", true);
                    }

                    cache_entry->lock.lock();
                    cache_entry->data.insert(cache_entry->data.end(), buf, buf + read_count);

                    bool end = false;
                    if (parse_data.done) {
                        cache_entry->state = CacheEntry::State::SUCCESS;
                        end = true;
                        if (parser.status_code != 200 && parser.status_code != 304) {
                            PLOGI << "Not caching " << parser.status_code;
                            proxy->cache_lock.lock();
                            proxy->cache.erase(cache_entry->key);
                            proxy->cache_lock.unlock();
                        }
                    }

                    if (cache_entry->listeners_count.get() == 0) {
                        PLOGI << "No listeners for " << cache_entry->key << " so no loading it";
                        proxy->cache_lock.lock();
                        if (cache_entry->listeners_count.get() == 0) {
                            proxy->cache.erase(cache_entry->key);
                            end = true;
                        } else {
                            PLOGI << "False alarm for " << cache_entry->key << " so do nothing";
                        }
                        proxy->cache_lock.unlock();
                    }

                    cache_entry->data_change_cv.broadcast();

                    cache_entry->lock.unlock();

                    if (end) {
                        break;
                    }
                } catch (const std::system_error& exception) {
                    PLOGW << "Server processing error: " << exception.what() << " " << resource_fd;

                    cache_entry->lock.lock();
                    cache_entry->data_change_cv.broadcast();
                    cache_entry->state = CacheEntry::State::FAILED;
                    cache_entry->lock.unlock();

                    break;
                }
            }

            delete[] buf;
            delete arg;
            close(resource_fd);

            return nullptr;
        }

        static std::pair<std::string, int> split_host(std::string host) {
            auto pos = host.find(':');
            if (pos != std::string::npos) {
                auto host_domain = host.substr(0, host.find(':'));
                host.erase(0, pos + 1);
                auto host_port = std::atoi(host.c_str());
                if (host_port == 0) {
                    throw_errno("failed to parse host_port", true);
                }
                return std::make_pair(host_domain, host_port);
            } else {
                return make_pair(host, 80);
            }
        }

        static std::string trim_url(const std::string& full_url, const std::string& host) {
            auto pos = full_url.find(host);
            if (pos != std::string::npos) {
                return full_url.substr(pos + host.size());
            }
            return full_url;
        }

        static int create_resource_request(const std::string& full_url,
                                           const std::unordered_map<std::string, std::string>& headers) {
            std::string host = headers.find("host")->second;
            auto[host_domain, host_port] = split_host(host);
            std::string url = trim_url(full_url, host);

            struct addrinfo hints{};
            hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
            hints.ai_socktype = SOCK_STREAM;/* TCP socket */
            hints.ai_flags = AI_PASSIVE;    /* For wildcard IP address */
            hints.ai_protocol = 0;          /* Any protocol */
            hints.ai_canonname = nullptr;
            hints.ai_addr = nullptr;
            hints.ai_next = nullptr;

            struct addrinfo *result;
            PLOGD << "getaddrinfo for " << full_url;
            auto rc = getaddrinfo(host_domain.c_str(), std::to_string(host_port).c_str(), &hints, &result);
            PLOGD << "getaddrinfo rc=" << rc;
            if (rc < 0) {
                throw_errno("failed to getaddrinfo()");
            }

            int resource_socket_fd;

            struct addrinfo *rp;
            int trycnt = 1;
            for (rp = result; rp != nullptr; rp = rp->ai_next) {
                resource_socket_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

                if (resource_socket_fd == -1) {
                    PLOGW << "failed to resolve " << full_url;
                    continue;
                }

                PLOGD << "connecting to " << full_url;
                rc = connect(resource_socket_fd, rp->ai_addr, rp->ai_addrlen);
                PLOGD << "connected to " << full_url;


                if (rc < 0) {
                    close(resource_socket_fd);
                    throw_errno("failed to connect");
                } else {
                    break;
                }

                trycnt++;
            }

            freeaddrinfo(result);

            PLOGI << "Connected resource fd=" << resource_socket_fd << " try=" << trycnt;

            std::stringstream http_response_stream;
            http_response_stream << "GET " << url << " HTTP/1.0" << HTTP_NEWLINE;
            for (const auto&[header_field, header_value]: headers) {
                http_response_stream << header_field << ": " << header_value << HTTP_NEWLINE;
            }
            http_response_stream << HTTP_NEWLINE;

            const auto str = http_response_stream.str();

            if (::write(resource_socket_fd, str.c_str(), str.length()) < 0) {
                close(resource_socket_fd);
                throw_errno("failed to write() request to resource socket");
            }

            PLOGI << "Written request to resource fd=" << resource_socket_fd;

            return resource_socket_fd;
        }

        static int connect_socket(int listen_port) {
            int listen_socket_fd = socket(AF_INET6, SOCK_STREAM, 0);
            if (listen_socket_fd < 0) {
                throw_errno("socket() failed");
            }

            int rc;
            rc = setsockopt(listen_socket_fd, SOL_SOCKET, SO_REUSEADDR, &ON, sizeof(ON));
            if (rc < 0) {
                close(listen_socket_fd);
                throw_errno("setsockopt() failed");
            }

            struct sockaddr_in6 addr{};
            addr.sin6_family = AF_INET6;
            inet_pton(AF_INET6, "::1", &addr.sin6_addr);
            addr.sin6_port = htons(listen_port);
            rc = bind(listen_socket_fd, (struct sockaddr *) &addr, sizeof(addr));
            if (rc < 0) {
                close(listen_socket_fd);
                throw_errno("bind() failed");
            }

            rc = listen(listen_socket_fd, BACKLOG);
            if (rc < 0) {
                close(listen_socket_fd);
                throw_errno("listen() failed");
            }

            return listen_socket_fd;
        }

        static void send_http_error(int fd, int code, const std::string& message) {
            std::stringstream http_response_stream;
            http_response_stream << "HTTP/1.0 " << code << " Proxy failed" << HTTP_NEWLINE << std::endl;
            http_response_stream << "Content-Length: " << message.size() + 1 << HTTP_NEWLINE << std::endl;
            http_response_stream << HTTP_NEWLINE << std::endl;
            http_response_stream << message << std::endl;
            http_response_stream << std::endl << std::endl;

            const auto str = http_response_stream.str();

            auto rc = write(fd, str.c_str(), str.size());

            if (rc < 0) {
                PLOGW << "Failed to send error message for " << fd;
            }
        }
    };
}

#endif //LAB32_PROXY_H
