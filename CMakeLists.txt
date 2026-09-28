cmake_minimum_required(VERSION 3.16)
project(api-analyzer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(api-analyzer api-analyzer.cpp)
target_link_libraries(api-analyzer PRIVATE Threads::Threads)
target_compile_options(api-analyzer PRIVATE -O2 -Wall -Wextra)