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
*/
#include "rtConnection.h"
#include "rtLog.h"
#include "rtMessage.h"

#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
printUsage()
{
  printf("\n");
  printf("Usage: rtsend [OPTIONS] [PAYLOAD]\n");
  printf("\t-b\t--broker <uri>   URI for broker (e.g. tcp://169.254.100.1:1234)\n");
  printf("\t-t\t--topic  <topic> Topic to send message on\n");
  printf("\n");
}

int main(int argc, char* argv[])
{
  char const*   uri;
  char const*   topic;
  char const*   payload;
  int           optionIndex;
  rtError       err;
  rtMessage     message;
  rtMessage     config;
  rtConnection  connection;

  uri = NULL;
  topic = NULL;
  payload = NULL;
  optionIndex = 0;
  err = 0;

  rtLog_SetLevel(RT_LOG_INFO);

  while (1)
  {
    static struct option longOptions[] = 
    {
      { "broker",   required_argument, 0, 'b' },
      { "topic",    required_argument, 0, 't' },
      { 0, 0, 0, 0 }
    };

    int c = getopt_long(argc, argv, "b:t:", longOptions, &optionIndex);
    if (c == -1)
      break;

    switch (c)
    {
      case 'b':
      uri = optarg;
      break;

      case 't':
      topic = optarg;
      break;

      case '?':
      break;

      default:
        break;
    }
  }

  if (!topic)
  {
    printf("missing topic\n");
    printUsage();
    exit(1);
  }

  if (!uri)
  {
    printf("missing uri\n");
    printUsage();
    exit(2);
  }

  if (optind < argc)
    payload = argv[optind];

  if (!payload)
  {
    printf("missing payload\n");
    printUsage();
    exit(3);
  }
  
  // rtLog_Info("topic:%s", topic);
  // rtLog_Info("uri  :%s", uri);
  // rtLog_Info("payload:%s", payload);
  err = rtMessage_FromBytes(&message, (uint8_t const  *)payload, strlen(payload));
  if (err != RT_OK)
  {
    rtLog_Error("failed to parse '%s' as rtMessage/JSON. %s",
      payload, rtStrError(err));
    exit(4);
  }

  rtMessage_Create(&config);
  rtMessage_SetString(config, "appname", "rtsend");
  rtMessage_SetString(config, "uri", uri);
  rtMessage_SetInt32(config, "start_router", 0);

  err = rtConnection_CreateWithConfig(&connection, config);
  if (err != RT_OK)
  {
    rtLog_Error("failed to create connection to router %s. %s",
      uri, rtStrError(err));
    exit(5);
  }

  err = rtConnection_SendMessage(connection, message, topic);
  if (err != RT_OK)
  {
    rtLog_Error("failed to send message on topic %s. %s",
      topic, rtStrError(err));
    exit(6);
  }

  rtLog_Info("send[%s]: %s", topic, rtStrError(err));

  rtMessage_Release(message);
  rtMessage_Release(config);
  rtConnection_Destroy(connection);

  return 0;
}
