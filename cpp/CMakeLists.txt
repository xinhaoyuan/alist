cmake_minimum_required(VERSION 3.0)
project(alist)

add_library(alist STATIC alist.cpp)

add_executable(alist_parse alist_parse.cpp)
target_link_libraries(alist_parse alist)
