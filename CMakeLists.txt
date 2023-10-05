cmake_minimum_required(VERSION 3.0)
project(yhchaos)

#add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdpart/yaml-cpp)
#add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdpart/jsoncpp-1.8.4)
#add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdpart/sqlite-3.28.0)
#add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdpart/tinyxml2-7.0.1)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include (cmake/utils.cmake)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -rdynamic -O3 -fPIC -ggdb -std=c++11 -Wall -Wno-deprecated -Werror -Wno-unused-function -Wno-builtin-macro-redefined -Wno-deprecated-declarations")
set(CMAKE_C_FLAGS "$ENV{CXXFLAGS} -rdynamic -O3 -fPIC -ggdb -std=c11 -Wall -Wno-deprecated -Werror -Wno-unused-function -Wno-builtin-macro-redefined -Wno-deprecated-declarations")

include_directories(.)
include_directories(/apps/yhchaos/include)
#include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdpart)
#include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdpart/yaml-cpp/include)
#include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdpart/jsoncpp-1.8.4/include)
#include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdpart/sqlite-3.28.0)
#include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdpart/tinyxml2-7.0.1)
link_directories(/apps/yhchaos/lib)
link_directories(/apps/yhchaos/lib64)

option(BUILD_TEST "ON for complile test" OFF)

find_package(Boost REQUIRED)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
endif()

find_package(MySQL REQUIRED)
if(MYSQL_FOUND)
    include_directories(${MYSQL_INCLUDE_DIR})
endif()

find_package(Protobuf)
if(Protobuf_FOUND)
    include_directories(${Protobuf_INCLUDE_DIRS})
endif()
find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
endif()

find_package(ZLIB REQUIRED)
if(ZLIB_FOUND)
    include_directories(${ZLIB_INCLUDE_DIR})
endif()

set(LIB_SRC
    yhchaos/network_address.cc
    yhchaos/bytebuffer.cc
    yhchaos/appconfig.cc
    yhchaos/db/watch_thread.cc
    yhchaos/db/cpp_mysql.cc
    yhchaos/db/cpp_redis.cc
    yhchaos/ds/util.cc
    yhchaos/environment.cc
    yhchaos/daemon.cc
    yhchaos/file_manager.cc
    yhchaos/coroutine.cc
    yhchaos/http/http.cc
    yhchaos/http/http_client.cc
    yhchaos/http/http_parser.cc
    yhchaos/http/http_session.cc
    yhchaos/http/httpsvr.cc
    yhchaos/http/cpp_servlet.cc
    yhchaos/http/servlets/config_servlet.cc
    yhchaos/http/servlets/status_servlet.cc
    yhchaos/http/ws_client.cc
    yhchaos/http/ws_session.cc
    yhchaos/http/ws_server.cc
    yhchaos/http/ws_servlet.cc
    yhchaos/hookfunc.cc
    yhchaos/iocoscheduler.cc
    yhchaos/library.cc
    yhchaos/log.cc
    yhchaos/modularity.cc
    yhchaos/mtx.cc
    yhchaos/dp_message.cc
    yhchaos/dp/dp_protocol.cc
    yhchaos/dp/dp_server.cc
    yhchaos/dp/dp_stream.cc
    yhchaos/coscheduler.cc
    yhchaos/sock.cc
    yhchaos/stream.cc
    yhchaos/streams/async_sock_stream.cc
    yhchaos/streams/sock_stream.cc
    yhchaos/streams/loadbalance.cc
    yhchaos/streams/service_discovery.cc
    yhchaos/streams/zlib_stream.cc
    yhchaos/tcpserver.cc
    yhchaos/timed_coroutine.cc
    yhchaos/thread.cc
    yhchaos/util.cc
    yhchaos/worker.cc
    yhchaos/appcase.cc
    yhchaos/zk_cli.cc
    )

ragelmaker(yhchaos/http/http11_parser.rl LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/yhchaos/http)
ragelmaker(yhchaos/http/httpclient_parser.rl LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/yhchaos/http)
ragelmaker(yhchaos/uri.rl LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/yhchaos)

protobufmaker(yhchaos/ns/ns_protobuf.proto LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR})
#PROTOBUF_GENERATE_CPP(PB_SRCS PB_HDRS yhchaos/ns/ns_protobuf.proto)
#message("****${PB_SRCS}***${PB_HDRS}****")
#list(APPEND LIB_SRC ${PB_SRCS})
#
#message(STATUS ${LIB_SRC})

add_library(yhchaos SHARED ${LIB_SRC})
force_redefine_file_macro_funor_sources(yhchaos)
#add_library(yhchaos_static STATIC ${LIB_SRC})
#SET_TARGET_PROPERTIES (yhchaos_static PROPERTIES OUTPUT_NAME "yhchaos")

#find_library(YAMLCPP yaml-cpp)
find_library(PTHREAD pthread)
#set(LIBS ${LIBS} yhchaos)
#set(LIBS ${LIBS} dl)
#set(LIBS ${LIBS} ${YAMLCPP})
#set(LIBS ${LIBS} ${PTHREAD})

set(LIBS
        yhchaos
        dl
        pthread
        yaml-cpp
        jsoncpp
        ${ZLIB_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        ${PROTOBUF_LIBRARIES}
        event
        hiredis_vip
        mysqlclient_r
        zookeeper_mt
        sqlite3
        tinyxml2
        jemalloc
        )
yhchaos_add_executable(test_util "tests/test_util.cc" yhchaos "${LIBS}")
if(BUILD_TEST)
yhchaos_add_executable(test1 "tests/test.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_appconfig "tests/test_appconfig.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_cppthread "tests/test_cppthread.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_coroutine "tests/test_coroutine.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_iocoscheduler "tests/test_io_co_scheduler.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_coscheduler "tests/test_coscheduler.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_hookfunc "tests/test_hookfunc.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_network_address "tests/test_network_address.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_sock "tests/test_sock.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_bytebuffer "tests/test_bytebuffer.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_http "tests/test_http.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_http_parser "tests/test_http_parser.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_tcpserver "tests/test_tcpserver.cc" yhchaos "${LIBS}")
yhchaos_add_executable(echo_server "examples/echo_server.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_httpsvr "tests/test_httpsvr.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_uri "tests/test_uri.cc" yhchaos "${LIBS}")
yhchaos_add_executable(my_http_server "samples/my_http_server.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_daemon "tests/test_daemon.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_environment "tests/test_envvironment.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_ws_server "tests/test_ws_server.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_ws_client "tests/test_ws_client.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_appcase "tests/test_appcase.cc" yhchaos "${LIBS}")

yhchaos_add_executable(test_http_client "tests/test_http_client.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_zlib_stream "tests/test_zlib_stream.cc" yhchaos "${LIBS}")

endif()
yhchaos_add_executable(test_crypto "tests/test_crypto.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_dp "tests/test_dp.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_mysql "tests/test_cppmysql.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_zkclient "tests/test_zookeeper.cc" yhchaos "${LIBS}")
yhchaos_add_executable(test_service_discovery "tests/test_service_discovery.cc" yhchaos "${LIBS}")

set(ORM_SRCS
    yhchaos/orm/table.cc
    yhchaos/orm/column.cc
    yhchaos/orm/index.cc
    yhchaos/orm/orm.cc
    yhchaos/orm/util.cc
    )
yhchaos_add_executable(orm "${ORM_SRCS}" yhchaos "${LIBS}")

add_library(test_module SHARED tests/test_module.cc)

yhchaos_add_executable(bin_yhchaos "yhchaos/main.cc" yhchaos "${LIBS}")
set_target_properties(bin_yhchaos PROPERTIES OUTPUT_NAME "yhchaos")

#add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/orm_out)
#set(OLIBS ${LIBS} orm_data)
#yhchaos_add_executable(test_orm "tests/test_orm.cc" orm_data "${OLIBS}")

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
