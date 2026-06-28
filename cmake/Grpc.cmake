# cmake/Grpc.cmake
#
# Generate protobuf/gRPC stubs dan link target agnc serve.

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(_AGNC_PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/proto/agnc/v1/agent.proto")
get_filename_component(_AGNC_PROTO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/proto" ABSOLUTE)

set(_AGNC_GRPC_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/grpc")
file(MAKE_DIRECTORY "${_AGNC_GRPC_GEN_DIR}")

# protoc mempertahankan struktur folder relatif -I (proto/agnc/v1/...).
set(_AGNC_PROTO_GEN_SUBDIR "agnc/v1")
set(_AGNC_PROTO_SRCS "${_AGNC_GRPC_GEN_DIR}/${_AGNC_PROTO_GEN_SUBDIR}/agent.pb.cc")
set(_AGNC_PROTO_HDRS "${_AGNC_GRPC_GEN_DIR}/${_AGNC_PROTO_GEN_SUBDIR}/agent.pb.h")
set(_AGNC_GRPC_SRCS "${_AGNC_GRPC_GEN_DIR}/${_AGNC_PROTO_GEN_SUBDIR}/agent.grpc.pb.cc")
set(_AGNC_GRPC_HDRS "${_AGNC_GRPC_GEN_DIR}/${_AGNC_PROTO_GEN_SUBDIR}/agent.grpc.pb.h")

add_custom_command(
    OUTPUT ${_AGNC_PROTO_SRCS} ${_AGNC_PROTO_HDRS} ${_AGNC_GRPC_SRCS} ${_AGNC_GRPC_HDRS}
    COMMAND protobuf::protoc
    ARGS --grpc_out=${_AGNC_GRPC_GEN_DIR}
         --cpp_out=${_AGNC_GRPC_GEN_DIR}
         -I ${_AGNC_PROTO_PATH}
         --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
         ${_AGNC_PROTO_FILE}
    DEPENDS ${_AGNC_PROTO_FILE}
    COMMENT "Generating gRPC stubs from agent.proto"
    VERBATIM
)

add_library(agnc_grpc_proto STATIC
    ${_AGNC_PROTO_SRCS}
    ${_AGNC_GRPC_SRCS}
)

target_include_directories(agnc_grpc_proto PUBLIC "${_AGNC_GRPC_GEN_DIR}")
target_link_libraries(agnc_grpc_proto PUBLIC protobuf::libprotobuf gRPC::grpc++)

function(agnc_enable_grpc_server target_name)
    target_sources(${target_name} PRIVATE
        src/grpc/serve.cpp
    )
    target_compile_definitions(${target_name} PRIVATE AGNC_HAS_GRPC=1)
    target_include_directories(${target_name} PRIVATE "${_AGNC_GRPC_GEN_DIR}")
    target_link_libraries(${target_name} PRIVATE agnc_grpc_proto gRPC::grpc++ gRPC::grpc++_reflection)
    set_target_properties(${target_name} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
endfunction()
