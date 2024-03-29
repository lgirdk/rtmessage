##########################################################################
# If not stated otherwise in this file or this component's LICENSE
# file the following copyright and licenses apply:
#
# Copyright 2019 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##########################################################################
cmake_minimum_required (VERSION 2.8.7)
project(rtmessage)
include(ExternalProject)
cmake_policy(SET CMP0015 NEW)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")
find_package(PkgConfig REQUIRED)

IF (NOT DEFINED CMAKE_INSTALL_BINDIR)
    SET(CMAKE_INSTALL_BINDIR ${CMAKE_INSTALL_PREFIX}/bin)
ENDIF ()

IF (NOT DEFINED CMAKE_INSTALL_LIBDIR)
    SET(CMAKE_INSTALL_LIBDIR ${CMAKE_INSTALL_PREFIX}/lib)
ENDIF ()

include(CMakePackageConfigHelpers)

SET (CMAKE_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/rtMessage")

configure_package_config_file(rtMessageConfig.cmake.in
  rtMessageConfig.cmake
  INSTALL_DESTINATION "${CMAKE_INSTALL_CMAKEDIR}")

write_basic_package_version_file(
  rtMessageConfigVersion.cmake
  VERSION 1.1.1
  COMPATIBILITY AnyNewerVersion )

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rtMessageConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/rtMessageConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_CMAKEDIR}" )

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set(RDKLOGGER_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../../rdklogger/)
set(BREAKPAD_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../../breakpadwrap/)
set(LIBEXCHANGER_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../../libexchanger)
set(LIBSYSWRAPPER_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../../libsyswrapper)

set(CMAKE_C_FLAGS_DEBUG     " -g -O0 -fno-inline ")
set(CMAKE_C_FLAGS_RELEASE   " ")
set(CMAKE_CXX_FLAGS_DEBUG   " -g -O0 -fno-inline ")
set(CMAKE_CXX_FLAGS_RELEASE " ")

if (CMAKE_VERSION VERSION_LESS "3.1")
  message("CMAKE ${CMAKE_CXX_COMPILER_ID}")
  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set (CMAKE_CXX_FLAGS "-std=gnu++11 ${CMAKE_CXX_FLAGS}")
  endif ()
else ()
  set (CMAKE_CXX_STANDARD 11)
endif ()

option(BUILD_FOR_DESKTOP "BUILD_FOR_DESKTOP" OFF)
option(BUILD_RTMESSAGE_LIB "BUILD_RTMESSAGE_LIB" ON)
option(ENABLE_RTMESSAGE_PROFILE "ENABLE_RTMESSAGE_PROFILE" OFF)
option(BUILD_RTMESSAGE_SAMPLE_APP "BUILD_RTMESSAGE_SAMPLE_APP" ON)
option(BUILD_RTMESSAGE_ROUTED "BUILD_RTMESSAGE_ROUTED" ON)
option(BUILD_DATAPROVIDER_LIB "BUILD_DATAPROVIDER_LIB" ON)
option(BUILD_DMCLI "BUILD_DMCLI" ON)
option(BUILD_DMCLI_SAMPLE_APP "BUILD_DMCLI_SAMPLE_APP" ON)
option(ENABLE_RDKLOGGER "ENABLE_RDKLOGGER" OFF)
option(INCLUDE_BREAKPAD "INCLUDE_BREAKPAD" OFF)
option(RDKC_BUILD "RDKC_BUILD" ON)
option(WITH_SPAKE2 "WITH_SPAKE2" OFF)
option(MSG_ROUNDTRIP_TIME "MSG_ROUNDTRIP_TIME" OFF)

set(OPENSSL_REQUIRED FALSE)

message("CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

if (WITH_SPAKE2)
  message("spake2 enabled")
  set(OPENSSL_REQUIRED TRUE)
  add_definitions(-DWITH_SPAKE2=1)
  if (BUILD_FOR_DESKTOP)
    add_definitions(-DWITH_SPAKE2_TEST_PIN=1)
    include_directories(${CMAKE_INSTALL_PREFIX}/include)
    link_directories(${CMAKE_INSTALL_PREFIX}/lib)
  endif (BUILD_FOR_DESKTOP)
  find_library(SPAKE2_LIBRARY spake2plus HINTS "${LIBEXCHANGER_PATH}/Release/src")
  find_library(PWD_LIBRARY pwd HINTS "${LIBEXCHANGER_PATH}/password/src")
endif (WITH_SPAKE2)

if (MSG_ROUNDTRIP_TIME)
  message("Message rountrip time calculation enabled")
  add_definitions(-DMSG_ROUNDTRIP_TIME=1)
endif (MSG_ROUNDTRIP_TIME)

if (OPENSSL_REQUIRED)
  message("openssl enabled")
  if( OPENSSL_ROOT_DIR )
    if( OPENSSL_VERSION )
        find_package(OpenSSL "${OPENSSL_VERSION}" EXACT REQUIRED PATH "${OPENSSL_ROOT_DIR}")
    else()
        find_library(SSL_LIBRARY ssl HINTS "${OPENSSL_ROOT_DIR}/lib")
        find_library(CRYPTO_LIBRARY crypto HINTS "${OPENSSL_ROOT_DIR}/lib")
        find_package(OpenSSL REQUIRED PATH "${OPENSSL_ROOT_DIR}")
    endif()
  else()
    if( OPENSSL_VERSION )
        find_package(OpenSSL "${OPENSSL_VERSION}" EXACT REQUIRED )
    else()
        find_package(OpenSSL REQUIRED )
    endif()
    include_directories("${OPENSSL_INCLUDE_DIR}")
  endif()
  include_directories("${OPENSSL_INCLUDE_DIR}")
  #message("OPENSSL_INCLUDE_DIR=${OPENSSL_INCLUDE_DIR}")
  #message("OPENSSL_CRYPTO_LIBRARY=${OPENSSL_CRYPTO_LIBRARY}")
  #message("OPENSSL_SSL_LIBRARY=${OPENSSL_SSL_LIBRARY}")
endif (OPENSSL_REQUIRED)

if (BUILD_FOR_DESKTOP)
  message("Building for desktop")
  ExternalProject_Add(
    cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG v1.6.0
    INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}
    INSTALL_COMMAND ""
    CONFIGURE_COMMAND ""
    BUILD_IN_SOURCE 1
    PREFIX deps
    CMAKE_ARGS "-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}"
    BUILD_COMMAND make)

  include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/deps/src)
  link_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/deps/src/cjson
    ${CMAKE_BINARY_DIR}
    ${LIBRARY_DIR})
else()
  find_package(cJSON REQUIRED)
  set(INCLUDE_DIR ${CMAKE_INSTALL_PREFIX}/include)
  set(LIBRARY_DIR ${CMAKE_INSTALL_PREFIX}/lib)
  message(STATUS "cjson include dir: ${CJSON_INCLUDE_DIRS}")
  if (ENABLE_RDKLOGGER)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${INCLUDE_DIR} ${CJSON_INCLUDE_DIRS} ${RDKLOGGER_PATH} ${RDKLOGGER_PATH}/include)
    link_directories(${CMAKE_CURRENT_SOURCE_DIR} ${LIBRARY_DIR} ${RDKLOGGER_PATH}/src/.libs/)
    add_definitions(-DENABLE_RDKLOGGER)
  else ()
    include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${INCLUDE_DIR} ${CJSON_INCLUDE_DIRS})
    link_directories(${CMAKE_CURRENT_SOURCE_DIR} ${LIBRARY_DIR})
  endif (ENABLE_RDKLOGGER)
  if (INCLUDE_BREAKPAD)
    include_directories(${BREAKPAD_PATH})
    link_directories(${BREAKPAD_PATH})
    add_definitions(-DINCLUDE_BREAKPAD)
  endif (INCLUDE_BREAKPAD)
  if (RDKC_BUILD)
    add_definitions(-DRDKC_BUILD)
  endif (RDKC_BUILD)
endif (BUILD_FOR_DESKTOP)

set(RTMESSAGE_LINK_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR} ${LIBRARY_DIR} ${RDKLOGGER_PATH}/src/.libs/ ${BREAKPAD_PATH})

if (ENABLE_RTMESSAGE_PROFILE)
  message("Enabling rtMessage profile")
endif (ENABLE_RTMESSAGE_PROFILE)

if (BUILD_RTMESSAGE_LIB)
    message("Building rtMessage lib")

    add_library(
      rtMessage
      SHARED
      rtConnection.c
      rtCipher.c
      rtLog.c
      rtError.c
      rtMessageHeader.c
      rtEncoder.c
      rtMessage.c
      rtSocket.c
      rtVector.c
      rtList.c
      rtTime.c
      rtSemaphore.c
      rtRetainable.c
      rtBase64.c
      rtThreadPool.c
      rtHashMap.c
      rtMemory.c
      local_benchmarking.c)

    if (ENABLE_RDKLOGGER)
      add_dependencies(rtMessage cjson rdklogger)
      target_link_libraries(rtMessage ${LIBRARY_LINKER_OPTIONS} -pthread -lcjson -llog4c -lrdkloggers)
    else ()
      add_dependencies(rtMessage cjson)
      target_link_libraries(rtMessage ${LIBRARY_LINKER_OPTIONS} -pthread -lcjson)
    endif (ENABLE_RDKLOGGER)

    if (OPENSSL_REQUIRED)
      target_link_libraries(rtMessage ${SSL_LIBRARY} ${CRYPTO_LIBRARY})
    endif (OPENSSL_REQUIRED)

    if (WITH_SPAKE2)
      include_directories(${LIBEXCHANGER_PATH}/src ${LIBEXCHANGER_PATH}/password/src ${LIBSYSWRAPPER_PATH}/source)
      target_link_libraries(rtMessage ${SPAKE2_LIBRARY})
    endif (WITH_SPAKE2)

endif (BUILD_RTMESSAGE_LIB)

if (BUILD_DATAPROVIDER_LIB)
  add_library(
    dataProvider
    SHARED
    dataProvider/dmProviderDatabase.cpp
    dataProvider/dmProviderHost.cpp
    dataProvider/dmPropertyInfo.cpp
    dataProvider/dmProviderInfo.cpp
    dataProvider/dmQueryResult.cpp
    dataProvider/dmProvider.cpp
    dataProvider/dmError.cpp
    dataProvider/dmValue.cpp
    dataProvider/dmClient.cpp)
  add_dependencies(
    dataProvider
    rtMessage)
  target_link_libraries(
    dataProvider
    rtMessage)
endif (BUILD_DATAPROVIDER_LIB)

if (BUILD_RTMESSAGE_ROUTED)
    message ("Building rtrouted")
    add_executable(rtrouted rtrouted.c rtRoutingTree.c)
    if (BUILD_FOR_DESKTOP)
      add_dependencies(rtrouted cjson)
    endif(BUILD_FOR_DESKTOP)
    if (INCLUDE_BREAKPAD)
      add_dependencies(rtrouted ${BREAKPAD_PATH})
      target_link_libraries(rtrouted ${LIBRARY_LINKER_OPTIONS} -lbreakpadwrap rtMessage)
    else ()
      target_link_libraries(rtrouted ${LIBRARY_LINKER_OPTIONS} rtMessage)
    endif (INCLUDE_BREAKPAD)
endif (BUILD_RTMESSAGE_ROUTED)

if (BUILD_DMCLI)
  add_executable(dmcli dataProvider/dmcli.cpp)
  if (BUILD_FOR_DESKTOP)
    add_dependencies(dmcli cjson rtMessage dataProvider)
    add_definitions("-DDEFAULT_DATAMODELDIR=\"${CMAKE_CURRENT_SOURCE_DIR}/dataProvider/data\"")
  else()
    add_definitions("-DDEFAULT_DATAMODELDIR=\"${RDK_FSROOT_PATH}/etc/model\"")
  endif (BUILD_FOR_DESKTOP)
  if (INCLUDE_BREAKPAD)
    add_dependencies(dmcli ${BREAKPAD_PATH})
    target_link_libraries(dmcli dataProvider -lbreakpadwrap rtMessage)
  else ()
    target_link_libraries(dmcli dataProvider rtMessage)
  endif (INCLUDE_BREAKPAD)
endif (BUILD_DMCLI)

if (BUILD_DMCLI_SAMPLE_APP)
    # sample provider for general
    add_executable(sample_provider_gen dataProvider/sample_provider_gen.cpp)
    if (BUILD_FOR_DESKTOP)
      add_dependencies(sample_provider_gen dataProvider)
    endif (BUILD_FOR_DESKTOP)
    target_link_libraries(sample_provider_gen ${LIBRARY_LINKER_OPTIONS} rtMessage dataProvider)
    # sample provider for wifi
    add_executable(sample_provider_wifi dataProvider/sample_provider_wifi.cpp)
    if (BUILD_FOR_DESKTOP)
      add_dependencies(sample_provider_wifi dataProvider)
    endif (BUILD_FOR_DESKTOP)
    target_link_libraries(sample_provider_wifi ${LIBRARY_LINKER_OPTIONS} rtMessage dataProvider)
endif (BUILD_DMCLI_SAMPLE_APP)

if (BUILD_RTMESSAGE_SAMPLE_APP)
    # sample_super
    add_executable(sample_super sample_super.c)
    if (BUILD_FOR_DESKTOP)
      add_dependencies(sample_super cjson)
    endif (BUILD_FOR_DESKTOP)
    add_dependencies(sample_super rtMessage)
    target_link_libraries(sample_super ${LIBRARY_LINKER_OPTIONS} rtMessage ${PWD_LIBRARY})

    # sample_send
    add_executable(sample_send sample_send.c)
    if (BUILD_FOR_DESKTOP)
      add_dependencies(sample_send cjson)
    endif (BUILD_FOR_DESKTOP)
    add_dependencies(sample_send rtMessage)
    target_link_libraries(sample_send ${LIBRARY_LINKER_OPTIONS} rtMessage)

    # sample_recv
    add_executable(sample_recv sample_recv.c)
    if (BUILD_FOR_DESKTOP)
      add_dependencies(sample_recv cjson)
    endif (BUILD_FOR_DESKTOP)
    add_dependencies(sample_send rtMessage)
    target_link_libraries(sample_recv ${LIBRARY_LINKER_OPTIONS} rtMessage)

    # sample_req
    add_executable(sample_req sample_req.c)
    if (BUILD_FOR_DESKTOP)
      add_dependencies(sample_req cjson)
    endif (BUILD_FOR_DESKTOP)
    add_dependencies(sample_send rtMessage)
    target_link_libraries(sample_req ${LIBRARY_LINKER_OPTIONS} rtMessage)

    # sample_res
    add_executable(sample_res sample_res.c)
    if (BUILD_FOR_DESKTOP)
      add_dependencies(sample_res cjson)
    endif (BUILD_FOR_DESKTOP)
    add_dependencies(sample_send rtMessage)
    target_link_libraries(sample_res ${LIBRARY_LINKER_OPTIONS} rtMessage)

    # rtsend
    add_executable(rtsend rtsend.c)
    if (BUILD_FOR_DESKTOP)
      add_dependencies(rtsend cjson)
    endif (BUILD_FOR_DESKTOP)
    add_dependencies(rtsend rtMessage)
    target_link_libraries(rtsend ${LIBRARY_LINKER_OPTIONS} rtMessage ${PWD_LIBRARY})

    # rtrouted_diag
    add_executable(rtrouted_diag diag_probe.c)
    add_dependencies(rtrouted_diag rtMessage)
    target_link_libraries(rtrouted_diag ${LIBRARY_LINKER_OPTIONS} rtMessage)
endif (BUILD_RTMESSAGE_SAMPLE_APP)

SET_TARGET_PROPERTIES (rtMessage PROPERTIES OUTPUT_NAME "rtMessage")

SET (RTMESSAGE_INSTALLTARGETS rtMessage)

INSTALL (TARGETS ${RTMESSAGE_INSTALLTARGETS} EXPORT rtMessage-targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

EXPORT (EXPORT rtMessage-targets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/rtMessage-targets.cmake"
)

INSTALL (EXPORT rtMessage-targets
    FILE
        rtMessage-targets.cmake
    DESTINATION
        "${CMAKE_INSTALL_CMAKEDIR}"
)

ADD_CUSTOM_TARGET(distclean COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/*.so dmcli sample_provider_* sample_super sample_req sample_res sample_send sample_recv rtrouted rtsend rtrouted_diag CMakeCache.txt)

install (TARGETS LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install (TARGETS ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

install (TARGETS rtrouted rtMessage rtrouted_diag sample_super RUNTIME DESTINATION bin LIBRARY DESTINATION lib)
install (
  FILES
  rtMessage.h
  rtMessageHeader.h
  rtError.h
  rtConnection.h
  rtVector.h
  rtRetainable.h
  rtLog.h
  rtList.h
  rtTime.h
  rtAtomic.h
  rtAdvisory.h
  rtThreadPool.h
  rtHashMap.h
  rtMemory.h
  rtm_discovery_api.h
  DESTINATION "include/rtmessage")

if (BUILD_FOR_DESKTOP)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/deps/src/cjson/libcjson.so ${CMAKE_CURRENT_BINARY_DIR}/deps/src/cjson/libcjson.so.1 ${CMAKE_CURRENT_BINARY_DIR}/deps/src/cjson/libcjson.so.1.6.0 DESTINATION lib)
endif (BUILD_FOR_DESKTOP)
