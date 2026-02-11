cmake_minimum_required(VERSION 3.28)
project(uDataPacketImportProxy VERSION 0.0.2 LANGUAGES CXX)
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
set(LIBRARY_SRC
    ${PROTO_SRC}
    src/proxy.cpp 
    src/proxyOptions.cpp
    src/grpcOptions.cpp
    src/backend.cpp
    src/backendOptions.cpp
    src/frontend.cpp
    src/frontendOptions.cpp)
set(HEADER_FILES
    src/backend.hpp
    src/frontend.hpp
    src/grpcOptions.hpp
    src/proxy.hpp
    src/backendOptions.hpp
    src/frontendOptions.hpp
    src/proxyOptions.hpp)
set(MODULE_FILES
    src/modules/logger.cppm
    src/modules/programOptions.cppm
    src/modules/metrics.cppm
    src/modules/otelSpdlogSink.cppm
    src/modules/getNow.cppm)

add_library(libuDataPacketImportProxy STATIC ${LIBRARY_SRC})
add_library(uDataPacketImportProxy::libuDataPacketImportProxy ALIAS libuDataPacketImportProxy)
set_target_properties(libuDataPacketImportProxy PROPERTIES
                      OUTPUT_NAME uDataPacketImportProxy
                      CXX_STANDARD 20
                      CXX_STANDARD_REQUIRED YES
                      CXX_EXTENSIONS NO)
target_include_directories(libuDataPacketImportProxy
                           #PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/modules>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
                          )
target_link_libraries(libuDataPacketImportProxy
                      PUBLIC TBB::tbb
                      PRIVATE gRPC::grpc
                              gRPC::grpc++
                              protobuf::libprotobuf
                              opentelemetry-cpp::otlp_http_log_record_exporter
                              opentelemetry-cpp::otlp_http_metric_exporter
                              spdlog::spdlog_header_only
                              Boost::headers
                              Boost::program_options
                              Threads::Threads)
target_sources(libuDataPacketImportProxy
               #PUBLIC ${LIBRARY_SRC}
               PUBLIC FILE_SET
                      HEADERS
                      BASE_DIRS
                        src
                      FILES
                        ${HEADER_FILES}
               PUBLIC FILE_SET
                      CXX_MODULES
                      BASE_DIRS
                        src/modules
                      FILES
                        ${MODULE_FILES}
               )
protobuf_generate(TARGET libuDataPacketImportProxy
                  LANGUAGE cpp 
                 )
protobuf_generate(TARGET libuDataPacketImportProxy
                  LANGUAGE grpc
                  PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
                  PLUGIN_OPTIONS generate_mock_code=true
                  GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
                 )   



add_executable(uDataPacketImportProxy src/main.cpp)
set_target_properties(uDataPacketImportProxy PROPERTIES
                      CXX_STANDARD 20
                      CXX_STANDARD_REQUIRED YES 
                      CXX_EXTENSIONS NO) 
target_include_directories(uDataPacketImportProxy
                           #PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
                          )
target_link_libraries(uDataPacketImportProxy
                      PUBLIC
                         TBB::tbb
                      PRIVATE
                         uDataPacketImportProxy::libuDataPacketImportProxy
                         gRPC::grpc
                         gRPC::grpc++
                         opentelemetry-cpp::otlp_http_log_record_exporter
                         opentelemetry-cpp::otlp_http_metric_exporter
                         spdlog::spdlog_header_only
                         Boost::headers
                         Boost::program_options
                         Threads::Threads)

##########################################################################################
#                                         Tests                                          #
##########################################################################################

add_executable(unitTests
               testing/grpc.cpp
               testing/proxy.cpp)
set_target_properties(unitTests PROPERTIES
                      CXX_STANDARD 20
                      CXX_STANDARD_REQUIRED YES
                      CXX_EXTENSIONS NO)
target_link_libraries(unitTests
                      PUBLIC
                         TBB::tbb
                      PRIVATE
                         uDataPacketImportProxy::libuDataPacketImportProxy 
                         gRPC::grpc
                         gRPC::grpc++
                         opentelemetry-cpp::otlp_http_metric_exporter
                         Threads::Threads
                         Catch2::Catch2 Catch2::Catch2WithMain)
target_include_directories(unitTests
                           #PRIVATE $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src/modules>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
add_test(NAME unitTests
         COMMAND unitTests)

##########################################################################################
#                                      Installation                                      #
##########################################################################################
include(CMakePackageConfigHelpers)
configure_file(${PROJECT_SOURCE_DIR}/cmake/uDataPacketImportProxy.cmake.in
               uDataPacketImportProxyConfig.cmake @ONLY)
write_basic_package_version_file(
   uDataPacketImportProxyConfigVersion.cmake
   VERSION ${PACKAGE_VERSION}
   COMPATIBILITY AnyNewerVersion)

include(GNUInstallDirs)
install(TARGETS libuDataPacketImportProxy
        EXPORT ${PROJECT_NAME}Targets
        FILE_SET HEADERS 
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILE_SET CXX_MODULES
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT libraries)
install(TARGETS uDataPacketImportProxy
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT applications)
export(EXPORT ${PROJECT_NAME}Targets
       FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/uDataPacketImportProxyConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/uDataPacketImportProxyConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/uDataPacketImportProxy)


##########################################################################################
#                                        Containers                                      #
##########################################################################################
include(CPackComponent)
if (${BUILD_FOR_DEBIAN})
   message("Will configure debian container")
   configure_file(${CMAKE_CURRENT_SOURCE_DIR}/scripts/Dockerfile.deb.in
                  ${CMAKE_CURRENT_BINARY_DIR}/Dockerfile.deb @ONLY)
   add_custom_target(${PROJECT_NAME}-deb
                     COMMENT "Creating deb file for ${PROJECT_NAME}"
                     COMMAND ${CMAKE_CPACK_COMMAND} -G DEB
                     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} VERBATIM)
   add_dependencies(${PROJECT_NAME}-deb uDataPacketImportProxy libuDataPacketImportProxy)
   find_program(buildah_EXECUTABLE buildah)
   if (buildah_EXECUTABLE)
      add_custom_target(buildah
                        COMMENT "Preparing the container with buildah"
                        COMMAND ${buildah_EXECUTABLE} bud -t udatapacketimportproxy:${PROJECT_VERSION} -t udatapacketimportproxy:latest -f ./Dockerfile.deb
                        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} VERBATIM)
      add_dependencies(buildah ${PROJECT_NAME}-deb)
   endif()
endif()

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
cpack_add_component(applications libraries
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
   #if (${BUILD_FOR_DEBIAN})
   #   find_program(DPKG_PROGRAM dpkg REQUIRED)
   #else()
   #   find_program(DPKG_PROGRAM dpkg)
   #endif()
   #if (DPKG_PROGRAM)
   #   list(APPEND CPACK_GENERATOR DEB)
   #   set(CPACK_DEBIAN_PACKAGE_DEPENDS
   #       "${CPACK_DEBIAN_PACKAGE_DEPENDS}")
   #   set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
   #endif()
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

