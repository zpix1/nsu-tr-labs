#ifndef LAB33_PROXY_H
#define LAB33_PROXY_H

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

#include "http_callbacks.h"

#include "utils.h"
#include "PollfdsManager.h"
#include "CacheEntry.h"
#include "WriteEntry.h"

#define HTTP_NEWLINE "\r\n"

namespace proxy {
    static const int ON = 1;

    class Proxy {
    private:
        static const size_t READ_BUF_SIZE = (1 << 20);
        static const size_t SEND_BUF_SIZE = (1 << 20);
        static const int BACKLOG = 300;

        int listen_port;
        int listen_socket_fd = -1;

        char *buf = nullptr;

        PollfdsManager pollfds_manager;

        std::unordered_map<std::string, std::shared_ptr<CacheEntry>> cache;
        std::unordered_map<int, std::shared_ptr<CacheEntry>> cache_loaders;

        std::unordered_map<int, http_parser *> client_parsers;
        std::unordered_map<int, http_parser *> resource_parsers;

        std::unordered_map<int, WriteEntry> writers;

    public:
        explicit Proxy(int listen_port) : listen_port(listen_port) {};

        [[noreturn]] void run() {
            ::signal(SIGPIPE, SIG_IGN);

            buf = new char[READ_BUF_SIZE];

            listen_socket_fd = connect_socket(listen_port);
            pollfds_manager.add_fd(listen_socket_fd);
            while (true) {
                auto active_fds = pollfds_manager.make_poll();
                for (const auto fd: active_fds) {
                    /*
                     * New client connected
                     */
                    if (fd == listen_socket_fd) {
                        connect();
                    } else
                    if (auto client_entry = client_parsers.find(fd); client_entry != client_parsers.end()) {
                        read_from_client(client_entry->first, client_entry->second);
                    } else
                    if (auto resource_entry = resource_parsers.find(fd); resource_entry != resource_parsers.end()) {
                        read_from_resource(resource_entry->first, resource_entry->second);
                    } else
                    if (auto write_entry = writers.find(fd); write_entry != writers.end()) {
                        write(write_entry->first, write_entry->second);
                    } else {
                        fprintf(stderr, "[%d] What is it?\n", fd);
                    }
                }
            }
        }

        void connect() {
            fprintf(stdout, "[%d] New connection\n", listen_socket_fd);
            int new_socket_fd;

            /*
             * Add all incoming connections into poll manager queue
             */
            new_socket_fd = accept(listen_socket_fd, nullptr, nullptr);
            if (new_socket_fd < 0) {
                if (errno == EWOULDBLOCK) {
                    throw_errno("accept blocking ???");
                }
                throw_errno("failed to accept()");
            }
            pollfds_manager.add_fd(new_socket_fd);

            /*
             * Create new parser for this client
             */
            auto *custom_data = new custom_data_t();
            auto *parser = new http_parser();

            http_parser_init(parser, HTTP_REQUEST);
            parser->data = custom_data;

            client_parsers[new_socket_fd] = parser;
        }

        void read_from_client(int fd, http_parser* parser) {
            fprintf(stdout, "[%d] Client\n", fd);
            try {
                http_parser_settings settings;
                http_parser_settings_init(&settings);
                settings.on_url = on_url_callback;
                settings.on_header_field = on_header_field;
                settings.on_header_value = on_header_value;
                settings.on_headers_complete = on_headers_complete;

                auto read_count = read(fd, buf, READ_BUF_SIZE);

                if (read_count < 0) {
                    delete static_cast<custom_data_t *>(parser->data);
                    delete parser;
                    throw_errno("failed to read() user request");
                }

                auto parsed_count = http_parser_execute(parser, &settings, buf, read_count);

                if (parsed_count != read_count) {
                    delete static_cast<custom_data_t *>(parser->data);
                    delete parser;
                    throw_errno("failed to parse user request", true);
                }

                auto *parse_data = static_cast<custom_data_t *>(parser->data);

                if (parse_data->done) {
                    if (parser->method != HTTP_GET) {
                        throw_errno("non GET methods are not supported (" + std::string(http_method_str((http_method)(parser->method))) + ")", true);
                    }

                    auto cache_key = parse_data->headers["host"] + parse_data->url;

                    if (auto cache_entry = cache.find(cache_key); cache_entry != cache.end()) {
                        auto cache_value = cache_entry->second;
                        fprintf(stdout, "[%d] Found %s (%lu bytes) in cache (%c), returning it back\n", fd,
                                cache_key.c_str(),
                                cache_value->data->size(),
                                cache_value->done ? 'L' : 'P');
                        if (cache_value->done) {
                            writers[fd] = WriteEntry{cache_value->data, 0, AfterWriteAction::CLEAN};
                            pollfds_manager.add_fd(fd, POLLOUT);
                            client_parsers.erase(fd);
                        } else {
                            cache_entry->second->waiting_sockets.insert(fd);
                        }
                    } else {
                        fprintf(stdout, "[%d] Not found %s in cache\n", fd, cache_key.c_str());
                        /*
                         * Create resource parser and add cache entry
                         */
                        {
                            int resource_fd = create_resource_request(parse_data->url, parse_data->headers);

                            auto cache_entry_ptr = std::make_shared<CacheEntry>();

                            cache_entry_ptr->key = cache_key;
                            cache_entry_ptr->waiting_sockets.insert(fd);
                            cache_entry_ptr->data = std::make_shared<std::vector<char>>();

                            cache[cache_key] = cache_entry_ptr;
                            cache_loaders[resource_fd] = cache_entry_ptr;

                            fprintf(stdout, "[%d] Added %s to cache\n", fd, cache_key.c_str());
                        }
                    }

                    /*
                     * We already read all we wanted
                     */
                    pollfds_manager.remove_fd(fd);
                    client_parsers.erase(fd);

                    delete static_cast<custom_data_t *>(parser->data);
                    delete parser;
                } else {
                    client_parsers[fd] = parser;
                }
            } catch (const std::system_error& error) {
                fprintf(stderr, "[%d] Client failed: %s\n", fd, error.what());
                send_http_error(fd, 500, error.what());
                pollfds_manager.remove_fd(fd);
                client_parsers.erase(fd);
                close(fd);
            }
        }

        void read_from_resource(int fd, http_parser* parser) {
            const auto& cache_value = cache_loaders.find(fd)->second;
            http_parser_settings settings;
            http_parser_settings_init(&settings);
            settings.on_message_complete = on_message_complete;

            auto before_size = cache_value->data->size();


            auto start = std::chrono::high_resolution_clock::now();

            cache_value->data->resize(before_size + READ_BUF_SIZE);

            auto finish = std::chrono::high_resolution_clock::now();
            auto how = std::chrono::duration_cast<std::chrono::seconds>(finish-start).count();
            if (how != 0) {
                fprintf(stderr, "[%d] [%d] Resize bytes in %lld s (%ld bytes)\n", fd, __LINE__, how, before_size + READ_BUF_SIZE);
            }

            auto read_count = read(fd, cache_value->data->data() + before_size, READ_BUF_SIZE);

            try {
                if (read_count < 0) {
                    throw_errno("failed to read() resource reply");
                }

                auto parsed_count = http_parser_execute(parser, &settings, cache_value->data->data() + before_size, read_count);

                if (parsed_count != read_count) {
                    throw_errno("failed to parse resource reply", true);
                }

                auto *parse_data = static_cast<custom_data_t *>(parser->data);

                cache_value->data->resize(before_size + parsed_count);

                /*
                 * If some listeners are not writers yet, create listening sockets writers
                 */
                for (auto waiting_fd: cache_value->waiting_sockets) {
                    if (writers.find(waiting_fd) == writers.end()) {
                        writers[waiting_fd] = WriteEntry{
                                cache_value->data,
                                0,
                                AfterWriteAction::PAUSE_FD,
                                cache_value
                        };
                        pollfds_manager.add_fd(waiting_fd, POLLOUT);
                    } else {
                        pollfds_manager.switch_fd(waiting_fd, POLLOUT);
                    }
                }


                if (parse_data->done) {
                    /*
                     * If it's done, clean waiters
                     */
                    for (auto waiting_fd: cache_value->waiting_sockets) {
                        writers[waiting_fd].action = AfterWriteAction::CLEAN;
                    }
                    cache_value->waiting_sockets.clear();

                    pollfds_manager.remove_fd(fd);
                    resource_parsers.erase(fd);

                    cache_loaders[fd]->done = true;

                    /*
                     * Do not cache non-successful requests
                     */
                    if (parser->status_code != 200 && parser->status_code != 304) {
                        cache.erase(cache_value->key);
                    }
                    cache_loaders.erase(fd);

                    close(fd);


                    delete static_cast<custom_data_t *>(parser->data);
                    delete parser;
                }
            } catch (const std::system_error& error) {
                fprintf(stderr, "[%d] Resource failed: %s\n", fd, error.what());

                for (auto waiting_fd: cache_value->waiting_sockets) {
                    send_http_error(fd, 500, error.what());
                    close(waiting_fd);
                }
                cache_value->waiting_sockets.clear();

                delete static_cast<custom_data_t *>(parser->data);
                delete parser;

                pollfds_manager.remove_fd(fd);
                resource_parsers.erase(fd);
                cache.erase(cache_value->key);
                cache_loaders.erase(fd);

                close(fd);
            }
        }

        void write(int fd, WriteEntry& writer) {
            if (writer.written == 0) {
                fprintf(stdout, "[%d] Started writing (%lu to write first round)\n", fd,
                        writer.data->size());
            }

            auto before = writer.written;

            size_t write_len = std::min((size_t) (writer.data->size() - writer.written),
                                        (size_t) SEND_BUF_SIZE);

            auto rc = ::write(fd, writer.data->data() + writer.written, write_len);
            if (writer.written == 0) {
                fprintf(stdout, "[%d] Started writing (rc=%ld)\n", fd, rc);
            }
            if (rc < 0) {
                if (rc == EWOULDBLOCK) {
                    /*
                     * Should never happen, but what if?
                     */
                    fprintf(stderr, "[%d] Why he blocks at write?\n", fd);
                }

                fprintf(stderr, "[%d] FATAL ERROR: Can't write to socket (%s), closing it\n", fd,
                        std::strerror(errno));
                pollfds_manager.remove_fd(fd);
                if (writer.entry) {
                    writer.entry->waiting_sockets.erase(fd);
                }
                writers.erase(fd);
                close(fd);
            } else {
                writer.written += rc;

                if (before / SEND_BUF_SIZE != writer.written / SEND_BUF_SIZE) {
                    fprintf(stdout, "[%d] Written %lu bytes\n", fd, writer.written);
                }

                if (writer.written == writer.data->size()) {
                    if (writer.action == AfterWriteAction::PAUSE_FD) {
                        pollfds_manager.switch_fd(fd, 0);
                    } else {
                        fprintf(stdout, "[%d] Ended writing\n", fd);
                        if (writer.action == AfterWriteAction::CLEAN) {
                            pollfds_manager.remove_fd(fd);
                            close(fd);
                        }
                        if (writer.action == AfterWriteAction::READ_FROM_RESOURCE) {
                            auto *custom_data = new custom_data_t();
                            auto *resource_parser = new http_parser();

                            http_parser_init(resource_parser, HTTP_RESPONSE);
                            resource_parser->data = custom_data;

                            resource_parsers[fd] = resource_parser;

                            pollfds_manager.switch_fd(fd, POLLIN);
                        }
                        writers.erase(fd);
                    }
                }
            }
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

        int create_resource_request(const std::string& full_url,
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

            auto rc = getaddrinfo(host_domain.c_str(), std::to_string(host_port).c_str(), &hints, &result);

            if (rc < 0) {
                throw_errno("failed to getaddrinfo()");
            }

            int resource_socket_fd;

            struct addrinfo *rp;
            int trycnt = 1;
            for (rp = result; rp != nullptr; rp = rp->ai_next) {
                resource_socket_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

                if (resource_socket_fd == -1) {
                    continue;
                }

                set_nonblock(resource_socket_fd);

                rc = ::connect(resource_socket_fd, rp->ai_addr, rp->ai_addrlen);

                if (rc < 0) {
                    if (errno == EINPROGRESS) {
                        fprintf(stdout, "[%d] Slow connect to resource\n", resource_socket_fd);
                        break;
                    } else {
                        close(resource_socket_fd);
                        throw_errno("failed to connect");
                    }
                } else {
                    break;
                }

                trycnt++;
            }

            freeaddrinfo(result);

            fprintf(stdout, "[%d] Connected resource fd with %d try\n", resource_socket_fd, trycnt);

            std::stringstream http_response_stream;
            http_response_stream << "GET " << url << " HTTP/1.0" << HTTP_NEWLINE;
            for (const auto&[header_field, header_value]: headers) {
                http_response_stream << header_field << ": " << header_value << HTTP_NEWLINE;
            }
            http_response_stream << HTTP_NEWLINE;

            const auto str = http_response_stream.str();

            /*
             * Uncomment to show generated request
             * fprintf(stdout, "---\n%s\n---", str.c_str());
             */

            writers[resource_socket_fd] = WriteEntry{
                    std::make_shared<std::vector<char>>(std::vector(str.begin(), str.end())),
                    0,
                    AfterWriteAction::READ_FROM_RESOURCE
            };
            pollfds_manager.add_fd(resource_socket_fd, POLLOUT);

            fprintf(stdout, "[%d] Created resource fd\n", resource_socket_fd);
            return resource_socket_fd;
        }

        static void set_nonblock(int fd) {
#ifdef __APPLE__
            auto rc = ioctl(fd, FIONBIO, &ON);
            if (rc < 0) {
                close(fd);
                throw_errno("failed to ioctl()");
            }
#else
            int fileflags;
            if ((fileflags = fcntl(fd, F_GETFL, 0)) == -1) {
                close(fd);
                throw_errno("failed to fcntl() get");
            }

            if (fcntl(fd, F_SETFL, fileflags | FNDELAY) == -1) {
                close(fd);
                throw_errno("failed to ioctl() set");
            }
#endif
        }

        static int connect_socket(int listen_port) {
            int listen_socket_fd = socket(AF_INET6, SOCK_STREAM, 0);
            if (listen_socket_fd < 0) {
                throw_errno("failed to socket()");
            }

            int rc;
            rc = setsockopt(listen_socket_fd, SOL_SOCKET, SO_REUSEADDR, &ON, sizeof(ON));
            if (rc < 0) {
                close(listen_socket_fd);
                throw_errno("failed to setsockopt()");
            }

            set_nonblock(listen_socket_fd);

            struct sockaddr_in6 addr{};
            addr.sin6_family = AF_INET6;
            inet_pton(AF_INET6, "::1", &addr.sin6_addr);
            addr.sin6_port = htons(listen_port);
            rc = bind(listen_socket_fd, (struct sockaddr *) &addr, sizeof(addr));
            if (rc < 0) {
                close(listen_socket_fd);
                throw_errno("failed to bind()");
            }

            rc = listen(listen_socket_fd, BACKLOG);
            if (rc < 0) {
                close(listen_socket_fd);
                throw_errno("failed to listen()");
            }

            return listen_socket_fd;
        }

        static void send_http_error(int fd, int code, const std::string& message) {
            std::stringstream http_response_stream;
            http_response_stream << "HTTP/1.0 " << code << " Proxy failed" << HTTP_NEWLINE;
            http_response_stream << "Content-Length: " << message.size() << HTTP_NEWLINE;
            http_response_stream << HTTP_NEWLINE;
            http_response_stream << message;
            http_response_stream << std::endl;
            fprintf(stderr, "[%d] Sending error message: '%s'\n", fd, message.c_str());

            const auto str = http_response_stream.str();

            auto rc = ::write(fd, str.c_str(), str.size());

            if (rc < 0) {
                fprintf(stderr, "[%d] Failed to send error message\n", fd);
            }
        }
    };
}

#endif //LAB33_PROXY_H
