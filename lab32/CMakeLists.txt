cmake_minimum_required(VERSION 3.20)
project(lab32)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")

include_directories(src/third-party/plog src/third-party/http-parser)

add_executable(lab32 src/main.cpp src/third-party/http-parser/http_parser.c)
