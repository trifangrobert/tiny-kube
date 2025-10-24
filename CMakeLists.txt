cmake_minimum_required(VERSION 3.15)
project(TinyKube LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PROTOBUF REQUIRED protobuf)
pkg_check_modules(GRPC REQUIRED grpc++)

# Find protoc compiler and grpc plugin
find_program(PROTOC_EXECUTABLE protoc REQUIRED)
find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin REQUIRED)

# Generate protobuf files
set(PROTO_FILES proto/control_plane.proto)
set(PROTO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(PROTO_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto)

file(MAKE_DIRECTORY ${PROTO_BINARY_DIR})

foreach(proto_file ${PROTO_FILES})
    get_filename_component(proto_name ${proto_file} NAME_WE)
    set(proto_srcs "${PROTO_BINARY_DIR}/${proto_name}.pb.cc")
    set(proto_hdrs "${PROTO_BINARY_DIR}/${proto_name}.pb.h")
    set(grpc_srcs "${PROTO_BINARY_DIR}/${proto_name}.grpc.pb.cc")
    set(grpc_hdrs "${PROTO_BINARY_DIR}/${proto_name}.grpc.pb.h")
    
    add_custom_command(
        OUTPUT ${proto_srcs} ${proto_hdrs} ${grpc_srcs} ${grpc_hdrs}
        COMMAND ${PROTOC_EXECUTABLE}
        ARGS --grpc_out=${PROTO_BINARY_DIR}
             --cpp_out=${PROTO_BINARY_DIR}
             --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
             --proto_path=${PROTO_SRC_DIR}
             ${CMAKE_CURRENT_SOURCE_DIR}/${proto_file}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${proto_file}
        COMMENT "Generating protobuf files for ${proto_file}"
    )
    
    list(APPEND PROTO_SRCS ${proto_srcs} ${grpc_srcs})
    list(APPEND PROTO_HDRS ${proto_hdrs} ${grpc_hdrs})
endforeach()

# Create a library for the generated protobuf code
add_library(proto_lib ${PROTO_SRCS} ${PROTO_HDRS})
target_include_directories(proto_lib PUBLIC 
    ${PROTO_BINARY_DIR} 
    ${PROTOBUF_INCLUDE_DIRS} 
    ${GRPC_INCLUDE_DIRS}
)

# Use the full library lists from pkg-config with proper library directories
target_link_directories(proto_lib PUBLIC ${PROTOBUF_LIBRARY_DIRS} ${GRPC_LIBRARY_DIRS})
target_link_libraries(proto_lib PUBLIC ${PROTOBUF_LIBRARIES} ${GRPC_LIBRARIES})
target_compile_options(proto_lib PUBLIC ${PROTOBUF_CFLAGS_OTHER} ${GRPC_CFLAGS_OTHER})

add_executable(tinykube_control src/control/main.cpp)
target_link_libraries(tinykube_control proto_lib)
target_include_directories(tinykube_control PRIVATE ${PROTO_BINARY_DIR})

add_executable(tinykube_agent src/agent/main.cpp)
target_link_libraries(tinykube_agent proto_lib)
target_include_directories(tinykube_agent PRIVATE ${PROTO_BINARY_DIR})