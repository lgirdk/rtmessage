/*
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
# This file contains material from pxCore which is Copyright 2005-2018 John Robinson
# Licensed under the Apache-2.0 license.
*/

// rtLog.h

#ifndef RT_LOG_H_
#define RT_LOG_H_
#include <stdint.h>
#ifdef RT_DEBUG
#define RT_LOG rtLog
#else
#define RT_LOG
#endif

#ifndef RT_LOGPREFIX
#define RT_LOGPREFIX "rt:"
#endif

#ifdef __APPLE__
typedef uint64_t rtThreadId;
#define RT_THREADID_FMT PRIu64
#elif defined WIN32
typedef unsigned long rtThreadId;
#define RT_THREADID_FMT "l"
#else
typedef int32_t rtThreadId;
#define RT_THREADID_FMT "d"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ENABLE_RDKLOGGER
#include "rdk_debug.h"
#endif

typedef enum
{
  RT_LOG_DEBUG = 0,
  RT_LOG_INFO  = 1,
  RT_LOG_WARN  = 2,
  RT_LOG_ERROR = 3,
  RT_LOG_FATAL = 4
} rtLogLevel;

typedef enum
{
  rtLog,
  rdkLog
}rtLogOption;

typedef void (*rtLogHandler)(rtLogLevel level, const char* file, int line, int threadId, char* message);

void rtLog_SetLevel(rtLogLevel l);
void rtLogSetLogHandler(rtLogHandler logHandler);
const char* rtLogLevelToString(rtLogLevel level);
rtLogLevel  rtLogLevelFromString(const char* s);
#ifdef ENABLE_RDKLOGGER
rdk_LogLevel rdkLogLevelFromrtLogLevel(rtLogLevel level);
#endif
void rtLog_SetOption(rtLogOption option);

rtThreadId rtThreadGetCurrentId();


#ifdef __GNUC__
#define RT_PRINTF_FORMAT(IDX, FIRST) __attribute__ ((format (printf, IDX, FIRST)))
#else
#define RT_PRINTF_FORMAT(IDX, FIRST)
#endif

// TODO would like this for to be hidden/private... something from Igor broke... 
void rtLogPrintf(rtLogLevel level, const char* file, int line, const char* format, ...) RT_PRINTF_FORMAT(4, 5);

#define rtLog(LEVEL, FORMAT, ...) do { rtLogPrintf(LEVEL, __FILE__, __LINE__, FORMAT, ## __VA_ARGS__); } while (0)
#define rtLog_Debug(FORMAT, ...) rtLog(RT_LOG_DEBUG, FORMAT, ## __VA_ARGS__)
#define rtLog_Info(FORMAT, ...) rtLog(RT_LOG_INFO, FORMAT, ## __VA_ARGS__)
#define rtLog_Warn(FORMAT, ...) rtLog(RT_LOG_WARN, FORMAT, ## __VA_ARGS__)
#define rtLog_Error(FORMAT, ...) rtLog(RT_LOG_ERROR, FORMAT, ## __VA_ARGS__)
#define rtLog_Fatal(FORMAT, ...) rtLog(RT_LOG_FATAL, FORMAT, ## __VA_ARGS__)

#ifdef __cplusplus
}
#endif
#endif 

