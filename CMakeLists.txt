PROJECT(darner)

SET(DARNER_VERSION 0.0.1)

CMAKE_MINIMUM_REQUIRED(VERSION 2.4)

CMAKE_POLICY(SET CMP0005 NEW) # don't complain about escaping the double-quotes below around DARNER_VERSION

ADD_DEFINITIONS(-Wall -Wno-return-type -O2 -g -DNDEBUG -DDARNER_VERSION="${DARNER_VERSION}")

INCLUDE_DIRECTORIES(include /usr/local/include/thrift)

ADD_EXECUTABLE(darner
               src/net/handler
               src/net/request
               src/util/log
               src/queue/queue
               src/main
               )

ADD_EXECUTABLE(bench_queue_flood
               bench/queue_flood
               src/queue/queue
               src/util/log
               )

ADD_EXECUTABLE(test
               src/net/request
               src/queue/istream
               src/queue/ostream
               src/queue/queue
               src/util/log
               tests/queue
               tests/request
               tests/main
               )

FIND_PACKAGE(Boost 1.46.1 COMPONENTS thread system program_options unit_test_framework filesystem)

TARGET_LINK_LIBRARIES(darner pthread ${Boost_LIBRARIES} leveldb)
TARGET_LINK_LIBRARIES(bench_queue_flood pthread ${Boost_LIBRARIES} leveldb)
TARGET_LINK_LIBRARIES(test pthread ${Boost_LIBRARIES} leveldb)
