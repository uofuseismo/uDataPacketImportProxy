cmake_minimum_required(VERSION 3.28)
project(uDataPacketImportProxy VERSION 0.0.1 LANGUAGES CXX)
enable_testing()

option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)
option(BUILD_FOR_DEBIAN "Build debian container for deployment" OFF)
option(BUILD_TESTS "Compile unit tests" ON) 
include(GenerateExportHeader)
#include(FetchContent)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()
if (NOT ${BUILD_SHARED_LIBS})
   set(Boost_USE_STATIC_LIBS ON)
endif()
find_package(absl CONFIG REQUIRED)
find_package(utf8_range CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(opentelemetry-cpp REQUIRED)
find_package(spdlog REQUIRED)
find_package(Boost REQUIRED headers program_options REQUIRED)
find_package(TBB COMPONENTS tbb REQUIRED)
find_package(Threads REQUIRED)
if (BUILD_TESTS)
   find_package(Catch2 3 REQUIRED)
endif()

set(PROTO_SRC
    uDataPacketImportAPI/v1/data_type.proto
    uDataPacketImportAPI/v1/stream_identifier.proto
    uDataPacketImportAPI/v1/packet.proto
    uDataPacketImportAPI/v1/publish_response.proto
    uDataPacketImportAPI/v1/frontend.proto
    #uDataPacketImportAPI/v1/subscribe_request.proto
    uDataPacketImportAPI/v1/backend.proto)

set(APPLICATION_SRC
    src/proxy.cpp 
    src/grpcOptions.cpp
    src/backendOptions.cpp
    src/frontendOptions.cpp
    src/proxyOptions.cpp
    src/frontend.cpp
    src/backend.cpp)

add_executable(uDataPacketImportProxy
               src/main.cpp ${APPLICATION_SRC} ${PROTO_SRC})
set_target_properties(uDataPacketImportProxy PROPERTIES
                      CXX_STANDARD 20
                      CXX_STANDARD_REQUIRED YES 
                      CXX_EXTENSIONS NO) 
target_include_directories(uDataPacketImportProxy
                           #PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
                          )
target_link_libraries(uDataPacketImportProxy
                      PRIVATE gRPC::grpc gRPC::grpc++
                              protobuf::libprotobuf
                              opentelemetry-cpp::prometheus_exporter
                              spdlog::spdlog_header_only
                              TBB::tbb
                              Boost::headers
                              Boost::program_options
                              Threads::Threads)
protobuf_generate(TARGET uDataPacketImportProxy
                  LANGUAGE cpp
                 )
protobuf_generate(TARGET uDataPacketImportProxy
                  LANGUAGE grpc
                  PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
                  PLUGIN_OPTIONS generate_mock_code=true
                  GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
                 )
