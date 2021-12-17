cmake_minimum_required(VERSION 3.20)
project(lab31)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")

add_executable(lab33 src/main.cpp src/proxy/Proxy.h src/proxy/http_parser.c)