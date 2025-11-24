cmake_minimum_required(VERSION 3.16)
project(decs_project LANGUAGES CXX)

add_definitions(-DCPPHTTPLIB_THREAD_POOL_COUNT=8)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(PostgreSQL REQUIRED)
include_directories(${PostgreSQL_INCLUDE_DIRS})
link_directories(${PostgreSQL_LIBRARY_DIRS})

include_directories(include third_party)

add_compile_options(-O3 -Wall -Wextra -pthread)

add_library(kvlib
  src/db.cpp
  src/http_server.cpp
  src/util.cpp
)
target_link_libraries(kvlib PRIVATE PostgreSQL::PostgreSQL)
target_include_directories(kvlib PUBLIC include)

add_executable(kvserver src/server_main.cpp)
target_link_libraries(kvserver PRIVATE kvlib)

add_executable(loadgen src/loadgen_main.cpp)
target_link_libraries(loadgen PRIVATE kvlib)


