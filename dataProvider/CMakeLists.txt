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
cmake_minimum_required(VERSION 2.8)
project(dmProvider)
include(ExternalProject)
cmake_policy(SET CMP0015 NEW)
set(CMAKE_CXX_STANDARD 11)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

option(BUILD_FOR_DESKTOP "BUILD_FOR_DESKTOP" ON)
option(BUILD_DMPROVIDER_LIB "BUILD_DMPROVIDER_LIB" ON)
option(BUILD_DM_SAMPLE_APP "BUILD_DM_SAMPLE_APP" ON)

set(CMAKE_C_FLAGS "")

ExternalProject_Add(
  cjson
  GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
  GIT_TAG v1.6.0
  INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}
  INSTALL_COMMAND ""
  CONFIGURE_COMMAND ""
  BUILD_IN_SOURCE 1
  PREFIX deps
  BUILD_COMMAND make)

if (BUILD_FOR_DESKTOP)
    set(MESSAGE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../)
    set(LIBRARY_DIR ${CJSON_DIR} ${MESSAGE_DIR})
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}
			${CJSON_DIR} 
            ${MESSAGE_DIR}
		       )
    link_directories(${CMAKE_CURRENT_SOURCE_DIR}
	             ${LIBRARY_DIR}
                    )
else()
     set(INCLUDE_DIR ${CMAKE_INSTALL_PREFIX}/include)
     set(LIBRARY_DIR ${CMAKE_INSTALL_PREFIX}/lib)
     include_directories(${CMAKE_CURRENT_SOURCE_DIR}
		    ${INCLUDE_DIR}
                    ${INCLUDE_DIR}/cjson
                    ${INCLUDE_DIR}/rtmessage
                   )
     link_directories(${CMAKE_CURRENT_SOURCE_DIR}
	         ${LIBRARY_DIR}
                )
endif (BUILD_FOR_DESKTOP)

set(DM_SOURCE_FILES dmValue.cpp dmProvider.cpp)

set(DM_APP_FILES dm_provider.cpp)

add_definitions(-DRT_PLATFORM_LINUX)

set(LIBRARY_LINKER_OPTIONS -Werror -Wall -Wextra -I. -fPIC)
set(DM_LINK_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR} ${LIBRARY_DIR})

if (ENABLE_DM_DEBUG)
    message("Enabling dm-provider debug")
    add_definitions(-DRT_RPC_DEBUG -DRT_DEBUG)
    set(CMAKE_CFLAGS "${CMAKE_C_FLAGS} -g -O0 -fno-inline")
else()
    set(CMAKE_CFLAGS "${CMAKE_C_FLAGS} -O2")

endif (ENABLE_DM_DEBUG)

if (ENABLE_DM_PROFILE)
    message("Enabling dm profile")
    set(CMAKE_CFLAGS "${CMAKE_C_FLAGS} -pg")
endif (ENABLE_DM_PROFILE)

if (BUILD_DMPROVIDER_LIB)
    message("Building dm provider lib")
    add_library(dmProvider_lib SHARED ${DM_SOURCE_FILES})
    set_target_properties(dmProvider_lib PROPERTIES OUTPUT_NAME "dmprovider")
    target_link_libraries(dmProvider_lib ${LIBRARY_LINKER_OPTIONS} -pthread -lcjson -lrtMessaging)
endif (BUILD_DMPROVIDER_LIB)

if (BUILD_DM_SAMPLE_APP)
    message ("Building dm sample app")
    set(CMAKE_CFLAGS " ${CMAKE_C_FLAGS}")
    link_directories(${DM_LINK_DIRECTORIES})
    add_executable(sample_app_dmcli ${DM_APP_FILES})
    set_target_properties(sample_app_dmcli PROPERTIES OUTPUT_NAME "dm_provider")
    target_link_libraries(sample_app_dmcli ${LIBRARY_LINKER_OPTIONS} dmProvider_lib)
    target_compile_definitions(sample_app_dmcli PRIVATE RT_PLATFORM_LINUX)
endif (BUILD_DM_SAMPLE_APP)

install (TARGETS LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install (TARGETS ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
