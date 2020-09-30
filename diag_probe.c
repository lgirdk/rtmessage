/*
  * If not stated otherwise in this file or this component's Licenses.txt file
  * the following copyright and licenses apply:
  *
  * Copyright 2019 RDK Management
  *
  * Licensed under the Apache License, Version 2.0 (the "License");
  * you may not use this file except in compliance with the License.
  * You may obtain a copy of the License at
  *
  * http://www.apache.org/licenses/LICENSE-2.0
  *
  * Unless required by applicable law or agreed to in writing, software
  * distributed under the License is distributed on an "AS IS" BASIS,
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
*/
#include "rtConnection.h"
#include "rtLog.h"
#include "rtMessage.h"
#include "rtrouter_diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RTROUTER_DIAG_CMD_ENABLE_VERBOSE_LOGS       "enableVerboseLogs"
#define RTROUTER_DIAG_CMD_DISABLE_VERBOSE_LOGS      "disableVerboseLogs"

#define RTROUTER_DIAG_CMD_LOG_ROUTING_TREE_STATS    "logRoutingTreeStats"
#define RTROUTER_DIAG_CMD_LOG_ROUTING_TREE_TOPICS   "logRoutingTreeTopics"
#define RTROUTER_DIAG_CMD_LOG_ROUTING_TREE_ROUTES   "logRoutingTreeRoutes"

#define RTROUTER_DIAG_CMD_ENABLE_TRAFFIC_MONITOR    "enableTrafficMonitor"
#define RTROUTER_DIAG_CMD_DISABLE_TRAFFIC_MONITOR   "disableTrafficMonitor"

#define RTROUTER_DIAG_CMD_RESET_BENCHMARKING_DATA   "resetBenchmarkData"
#define RTROUTER_DIAG_CMD_DUMP_BENCHMARKING_DATA    "dumpBenchmarkData"

int main(int argc, char * argv[])
{
    printf("%s",
            "----------\nHelp:\n"
            "Syntax: rtm_diag_probe <command>\n"
            "Following commands are supported:\n"
            "enableVerboseLogs      - Enable debug level logs in router.\n"
            "disableVerboseLogs     - Disable debug level logs in router.\n"
            "logRoutingStats        - Log routing tree stats.\n"
            "logRoutingTopics       - Log routing tree topics.\n"
            "logRoutingRoutes       - Log routing tree routes.\n"
            "enableTrafficMonitor   - Enable bus traffic logging.\n"
            "disableTrafficMonitor  - Disable bus traffic logging.\n"
            "dumpBenchmarkData      - Dump raw benchmark data to rtrouted logs.\n"
            "resetBenchmarkData     - Reset data collected so far for benchmarking.\n"
            "----------\n"

          );
  if(1 != argc)
  {
      rtConnection con;

      rtLog_SetLevel(RT_LOG_INFO);
      rtConnection_Create(&con, "APP2", "unix:///tmp/rtrouted");
      rtMessage out;
      rtMessage_Create(&out);
      rtMessage_SetString(out, RTROUTER_DIAG_CMD_KEY, argv[1]);
      rtConnection_SendMessage(con, out, "_RTROUTED.INBOX.DIAG");
      sleep(1);
      rtConnection_Destroy(con);
  }
  return 0;
}
