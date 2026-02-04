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
    uDataPacketImportAPI/v1/subscription_request.proto
    uDataPacketImportAPI/v1/backend.proto)
set(COMMON_SRC
    ${PROTO_SRC}
    src/proxy.cpp 
    src/grpcOptions.cpp
    src/backendOptions.cpp
    src/frontendOptions.cpp
    src/proxyOptions.cpp
    src/frontend.cpp
    src/backend.cpp)

set(APPLICATION_SRC
    src/otelSpdlogSink.cpp
    ${COMMON_SRC})


add_executable(uDataPacketImportProxy)
target_sources(uDataPacketImportProxy
               PUBLIC src/main.cpp ${APPLICATION_SRC} ${PROTO_SRC}
               PRIVATE FILE_SET
                       HEADERS
                       BASE_DIRS
                         src
               PRIVATE FILE_SET
                       CXX_MODULES
                       BASE_DIRS
                         src/modules
                       FILES
                         src/modules/logger.cppm
                         src/modules/programOptions.cppm
                         src/modules/metrics.cppm
                         src/modules/otelSpdlogSink.cppm
                         src/modules/getNow.cppm)
set_target_properties(uDataPacketImportProxy PROPERTIES
                      CXX_STANDARD 20
                      CXX_STANDARD_REQUIRED YES 
                      CXX_EXTENSIONS NO) 
target_include_directories(uDataPacketImportProxy
                           #PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
                          )
target_link_libraries(uDataPacketImportProxy
                      PRIVATE gRPC::grpc
                              gRPC::grpc++
                              protobuf::libprotobuf
                              opentelemetry-cpp::otlp_http_log_record_exporter
                              opentelemetry-cpp::otlp_http_metric_exporter
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

##########################################################################################
#                                         Tests                                          #
##########################################################################################

add_executable(unitTests)
target_sources(unitTests
               PUBLIC testing/proxy.cpp
                      ${COMMON_SRC}
               PRIVATE FILE_SET
                       HEADERS
                       BASE_DIRS
                         src 
               PRIVATE FILE_SET
                       CXX_MODULES
                       BASE_DIRS
                         src/modules
                       FILES
                         src/modules/metrics.cppm
                         src/modules/programOptions.cppm)
set_target_properties(uDataPacketImportProxy PROPERTIES
                      CXX_STANDARD 20
                      CXX_STANDARD_REQUIRED YES 
                      CXX_EXTENSIONS NO) 

set_target_properties(unitTests PROPERTIES
                      CXX_STANDARD 20
                      CXX_STANDARD_REQUIRED YES
                      CXX_EXTENSIONS NO)
target_link_libraries(unitTests
                      gRPC::grpc
                      gRPC::grpc++
                      #gRPC::grpcpp_otel_plugin
                      opentelemetry-cpp::otlp_http_metric_exporter
                      TBB::tbb
                      Threads::Threads
                      Catch2::Catch2 Catch2::Catch2WithMain)
target_include_directories(unitTests
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>)
add_test(NAME unitTests
         COMMAND unitTests)
protobuf_generate(TARGET unitTests
                  LANGUAGE cpp
                 )
protobuf_generate(TARGET unitTests
                  LANGUAGE grpc
                  PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
                  PLUGIN_OPTIONS generate_mock_code=true
                  GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
                 )

##########################################################################################
#                                      Installation                                      #
##########################################################################################
include(GNUInstallDirs)
install(TARGETS uDataPacketImportProxy
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT applications)

##########################################################################################
#                                     CPACK Packaging                                    #
##########################################################################################
include(CPackComponent)
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VENDOR "UUSS")
set(CPACK_PACKAGE_CONTACT "ben.baker@utah.edu")
set(CPACK_PACKAGE_LICENSE "MIT")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Serves as a common endpoint for various external seismic data packet ingestion mechanisms in the UUSS Kubernetes environment.")
set(CPACK_PACKAGE_INSTALL_DIRECTORY ${CPACK_PACKAGE_NAME})
set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
cpack_add_component(applications
                    DISPLAY_NAME "uDataPacketImportProxy applications")
set(CPACK_GENERATOR ZIP TGZ)
if (WIN32)
   list(APPEND CPACK_GENERATOR WIX)
elseif (APPLE)
   list(APPEND CPACK_GENERATOR productbuild)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
   find_program(RPMBUILD_PROGRAM rpmbuild)
   if (RPMBUILD_PROGRAM)
      list(APPEND CPACK_GENERATOR RPM)
   endif()
   if (${BUILD_FOR_DEBIAN})
      find_program(DPKG_PROGRAM dpkg REQUIRED)
   else()
      find_program(DPKG_PROGRAM dpkg)
   endif()
   if (DPKG_PROGRAM)
      list(APPEND CPACK_GENERATOR DEB)
      set(CPACK_DEBIAN_PACKAGE_DEPENDS
          "${CPACK_DEBIAN_PACKAGE_DEPENDS}" libtbb (>=2022.0.0))
      set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
   endif()
else()
   message("Unknown operating system")
endif()
set(CPACK_SOURCE_IGNORE_FILES
  /\\.git/
  \\.swp
  \\.orig
  /CMakeLists\\.txt\\.user
  /private/
)
include(CPack) # Put this last!

