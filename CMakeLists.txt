cmake_minimum_required(VERSION 3.3)

project(
  AirReplay
  VERSION 0.0.1
  LANGUAGES CXX)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED On)
set(CMAKE_CXX_EXTENSIONS Off)

# necessary if the target system uses shared libraries
# https://www.collabora.com/about-us/blog/2014/10/01/dynamic-relocs,-runtime-overflows-and-fpic/
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(POLICY CMP0077)
  # Allow parent project to override options of children obtain via FetchContent
  # or add_subdirectory.
  cmake_policy(SET CMP0077 NEW)
endif()

option(KUDU_HOME "Specifies the home directory of kudu if AirReplay is used with kudu so kudu's built protobuf can be used" "")

# use the protobuf library built by kudu instead of using the systemwide one
if (KUDU_HOME)
  message(INFO "kudu home is ${KUDU_HOME}")
  set(PROTOBUF_PREFIX "${KUDU_HOME}/thirdparty/installed/uninstrumented")
  set(Protobuf_INCLUDE_DIR "${PROTOBUF_PREFIX}/include")
  set(Protobuf_LIBRARY_DEBUG "${PROTOBUF_PREFIX}/lib/libprotobuf.a")
  set(Protobuf_LIBRARY_RELEASE "${PROTOBUF_PREFIX}//lib/libprotobuf.a")

  set(Protobuf_LITE_LIBRARY_DEBUG "${PROTOBUF_PREFIX}/lib/libprotobuf-lite.a")
  set(Protobuf_LITE_LIBRARY_RELEASE "${PROTOBUF_PREFIX}/lib/libprotobuf-lite.a")

  set(Protobuf_PROTOC_EXECUTABLE "${PROTOBUF_PREFIX}/bin/protoc")
  set(Protobuf_PROTOC_LIBRARY_DEBUG "${PROTOBUF_PREFIX}/lib/libprotoc.a")
  set(Protobuf_PROTOC_LIBRARY_RELEASE "${PROTOBUF_PREFIX}/lib/libprotoc.a")
endif()

find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

if ("${Protobuf_VERSION}" VERSION_LESS 3.6.0)
  message(WARNING "Protobuf version should be >= 3.6.0 but is ${Protobuf_VERSION}")
  add_compile_definitions(USE_OLD_PROTOBUF)
  message(WARNING "Turning on protobuf compatibility mode")
endif()

# # specify sources and project root
PROTOBUF_GENERATE_CPP(
  PERSISTENT_VARS_PROTO_SRCS PERSISTENT_VARS_PROTO_HDRS
    airreplay/airreplay.proto)

  # set(options EXPORTED_SHARED)
  # set(one_value_args COMPILE_FLAGS EXPORTED_OUTPUT_NAME EXPORTED_OUTPUT_DIRECTORY)
  # set(multi_value_args SRCS DEPS EXPORTED_DEPS NONLINK_DEPS)
  # cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})
  # if(ARG_UNPARSED_ARGUMENTS)
  #   message(SEND_ERROR "Error: unrecognized arguments: ${ARG_UNPARSED_ARGUMENTS}")
  # endif()

  # # First add the regular version of the library. It uses
  # # whatever linkage was defined globally.
  # add_library(${LIB_NAME} ${ARG_SRCS})
  # if(ARG_COMPILE_FLAGS)
  #   set_target_properties(${LIB_NAME}
  #     PROPERTIES COMPILE_FLAGS ${ARG_COMPILE_FLAGS})
  # endif()
  # target_link_libraries(${LIB_NAME} ${ARG_DEPS})
  # if(ARG_NONLINK_DEPS)
  #   add_dependencies(${LIB_NAME} ${ARG_NONLINK_DEPS})
  # endif()

  # # Now start setting up the exported variant.
  # set(EXPORTED_LIB_NAME ${LIB_NAME}_exported)
  # if(ARG_EXPORTED_SHARED)
  #   # Leaf library.
  #   set(EXPORTED_LINKAGE "SHARED")
  #   set(EXPORTED_LINK_PRIVATE "LINK_PRIVATE")
  # else()
  #   # Internal library.
  #   set(EXPORTED_LINKAGE "STATIC")
  #   set(EXPORTED_LINK_PRIVATE)
  # endif()
  # add_library(${EXPORTED_LIB_NAME} ${EXPORTED_LINKAGE} ${ARG_SRCS})

# export protos as a linkable library
# ADD_EXPORTABLE_LIBRARY(airreplay_proto
#   SRCS ${PERSISTENT_VARS_PROTO_SRCS}
#   DEPS ${PERSISTENT_VARS_PROTO_LIBS}
#   NONLINK_DEPS ${PERSISTENT_VARS_PROTO_TGTS})

add_library(airreplay_proto ${PERSISTENT_VARS_PROTO_SRCS})
target_link_libraries(airreplay_proto ${Protobuf_LIBRARIES})

set(AIRREPLAY_SRCS
  airreplay/trace.cc
  airreplay/airreplay.cc
  airreplay/external_replayer.cc
  airreplay/utils.cc
  airreplay/socket.cc
  airreplay/mock_socket_traffic.cc
)
add_library(airreplay SHARED ${AIRREPLAY_SRCS} ${PERSISTENT_VARS_PROTO_SRCS})
target_link_libraries(airreplay airreplay_proto)

# These are for backtrace.hpp. Make sure GNU/binutils are installed (apt-get install binutils-dev)
target_link_libraries(airreplay bfd dl)

if (KUDU_HOME)
  # needed for sparsehash/dense_hash_map, required in schema.h
  target_include_directories(airreplay PUBLIC "${KUDU_HOME}/thirdparty/installed/common/include")
  target_include_directories(airreplay PUBLIC "${KUDU_HOME}/src")
  # aaa this is bad.
  target_include_directories(airreplay PUBLIC "${KUDU_HOME}/build/debug/src")
endif()

<<<<<<< HEAD
# EXECUTIBLES mock-socket-replayer

add_executable(socketreplay airreplay/socketreplay_main.cc)
target_link_libraries(socketreplay airreplay airreplay_proto glog)

# TESTS
=======
>>>>>>> 2599ed9 ([temporary] commit backtrace to allow rebase)

add_executable(serde-test airreplay/serde-test.cc airreplay/gtest_main.cc ${PERSISTENT_VARS_PROTO_SRCS})
set_target_properties(serde-test PROPERTIES EXCLUDE_FROM_ALL 1 EXCLUDE_FROM_DEFAULT_BUILD 1)
add_dependencies(serde-test not-up-to-date)
target_include_directories(serde-test PUBLIC .)
target_link_libraries(serde-test
airreplay
${Protobuf_LIBRARIES}
airreplay_proto
# kudu built gmock which has gtest built in but it did not build gtest separately
# so we do not have gtest and gtest main but gmock has everything we need.
# when factoring this out into a separate lib, we can use regular cmake machinery to resolve this
# but for now this should be fine.
gmock
# todo: why do I need glog?
glog
gflags)


add_custom_target(not-up-to-date
    COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --red "Attempt to build an AirReplay dependency or test that is not up to date with AirReplay library"
)

add_executable(airr-test airreplay/airr-test.cc airreplay/airr-test-async.cc  airreplay/gtest_main.cc ${PERSISTENT_VARS_PROTO_SRCS})
set_target_properties(airr-test PROPERTIES EXCLUDE_FROM_ALL 1 EXCLUDE_FROM_DEFAULT_BUILD 1)
add_dependencies(airr-test not-up-to-date)
target_include_directories(airr-test PUBLIC .)
target_link_libraries(airr-test
airreplay
${Protobuf_LIBRARIES}
airreplay_proto
# gutil # this is ofr SanityTest callback stuff

# kudu built gmock which has gtest built in but it did not build gtest separately
# so we do not have gtest and gtest main but gmock has everything we need.
# when factoring this out into a separate lib, we can use regular cmake machinery to resolve this
# but for now this should be fine.
gmock
# todo: why do I need glog?
glog
gflags
)


set(INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/install")
set(INSTALL_HEADER_DIR "${INSTALL_DIR}/include/airreplay")
set(INSTALL_LIB_DIR "${INSTALL_DIR}/lib")

# install headers
install(
    DIRECTORY "airreplay/"
    DESTINATION "${INSTALL_HEADER_DIR}"
    FILES_MATCHING
    PATTERN "*.h"
)
foreach(ITEM ${PERSISTENT_VARS_PROTO_HDRS})
  install(
    FILES ${ITEM}
    DESTINATION "${INSTALL_HEADER_DIR}"
  )
endforeach()

# install libraries
install(
    TARGETS airreplay
    DESTINATION "${INSTALL_LIB_DIR}"
)
