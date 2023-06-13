# specify sources and project root
PROTOBUF_GENERATE_CPP(
  PERSISTENT_VARS_PROTO_SRCS PERSISTENT_VARS_PROTO_HDRS PERSISTENT_VARS_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/..
  PROTO_FILES
    airreplay.proto)

# add libs to link in
set(PERSISTENT_VARS_PROTO_LIBS
  protobuf)

# export protos as a linkable library
ADD_EXPORTABLE_LIBRARY(airreplay_proto
  SRCS ${PERSISTENT_VARS_PROTO_SRCS}
  DEPS ${PERSISTENT_VARS_PROTO_LIBS}
  NONLINK_DEPS ${PERSISTENT_VARS_PROTO_TGTS})

set(AIRREPLAY_SRCS
  airreplay.cc
  external_replayer.cc
)
add_library(airreplay ${AIRREPLAY_SRCS})
# target_link_libraries(consensus)

add_executable(serde-test serde-test.cc gtest_main.cc ${PERSISTENT_VARS_PROTO_SRCS})
target_include_directories(serde-test PUBLIC .)
target_link_libraries(serde-test
airreplay
protobuf
airreplay_proto

# kudu built gmock which has gtest built in but it did not build gtest separately
# so we do not have gtest and gtest main but gmock has everything we need.
# when factoring this out into a separate lib, we can use regular cmake machinery to resolve this
# but for now this should be fine.
gmock
# todo: why do I need glog?
glog
gflags)



# execute_process(
#     COMMAND bash "-c" "protoc --plugin=protoc-gen-cpp-grpc=/home/narek/grpc_install/LIB_INSTALLED/bin/grpc_cpp_plugin  --proto_path=../../src/airreplay/ --cpp-grpc_out=./src/airreplay ../../src/airreplay/airreplay.proto"
# )
# . rpc-test.cc
add_executable(airr-test airr-test.cc airr-test-async.cc  gtest_main.cc ${PERSISTENT_VARS_PROTO_SRCS})
target_include_directories(airr-test PUBLIC .)
target_link_libraries(airr-test
airreplay
protobuf
airreplay_proto
gutil # this is ofr SanityTest callback stuff

# kudu built gmock which has gtest built in but it did not build gtest separately
# so we do not have gtest and gtest main but gmock has everything we need.
# when factoring this out into a separate lib, we can use regular cmake machinery to resolve this
# but for now this should be fine.
gmock
# todo: why do I need glog?
glog
gflags
)
# get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
# protobuf_generate(TARGET myproto LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}")