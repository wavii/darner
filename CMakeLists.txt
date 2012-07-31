PROJECT(darner)

CMAKE_MINIMUM_REQUIRED(VERSION 2.4)

# Make sure that we respect any absolute library paths rather than splitting
# them into a -L/some/path -llib
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

SET(CMAKE_VERBOSE_MAKEFILE ON)

ADD_DEFINITIONS(-Wall -Wno-return-type -O2 -g -DNDEBUG)

INCLUDE_DIRECTORIES(include /usr/local/include/thrift)

ADD_EXECUTABLE(bench_queue_flood
               bench/queue_flood
               )

ADD_EXECUTABLE(test
               tests/queue
               tests/fs
               tests/main
               )

FIND_PACKAGE(Boost 1.46.1 COMPONENTS thread system program_options unit_test_framework filesystem)

TARGET_LINK_LIBRARIES(bench_queue_flood pthread ${Boost_LIBRARIES} leveldb)
TARGET_LINK_LIBRARIES(test pthread ${Boost_LIBRARIES} leveldb)
