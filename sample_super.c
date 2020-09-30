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
#include "rtVector.h"
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>

#define MAX_TOPICS 5
#define MAX_TOPIC_LEN 256

static int messageReceived = 0;

void printHelp()
{
  printf("sample_super: this app allows you to send a message to a topic and/or receive a message on a topic.\n");
  printf("\t -S --send  send a message\n");
  printf("\t -R --request  send a request and get response\n");
  printf("\t -L --listen for messages\n");
  printf("\t -t --topic  topic path (default: \"A.B.C\")\n");
  printf("\t -m --msg  text message to send (default: \"Hello World!\")\n");
  printf("\t -w --wait  seconds to wait before exiting (default: 30)\n");
  printf("\t -s --socket socket path of rtrouted (default: \"tcp://127.0.0.1:10001\")\n");
  printf("\t -l --log-level log level (default: RT_LOG_DEBUG)\n");
  printf("\t -h --help show help info\n");
  fflush(stdout);
}

void logHandler(rtLogLevel level, const char* file, int line, int threadId, char* message)
{
  (void)level;
  (void)file;
  (void)line;
  (void)threadId;
  printf("%d: %s\n", getpid(), message);
}

void onMessage(rtMessageHeader const* hdr, uint8_t const* buff, uint32_t n, void* closure)
{
  rtConnection con = (rtConnection) closure;
  char* buff2 = NULL;
  uint32_t buff_length = 0;

  rtMessage req;
  rtMessage_FromBytes(&req, buff, n);

  rtMessage_ToString(req, &buff2, &buff_length);
  rtLog_Info("sample_super got message:%.*s", buff_length, buff2);
  free(buff2);

  rtMessage_Release(req);

  if (rtMessageHeader_IsRequest(hdr))
  {
    rtMessage res;
    rtLog_Info("sample_super message was request so sending response.");
    rtMessage_Create(&res);
    rtMessage_SetString(res, "reply", "Success");
    rtConnection_SendResponse(con, hdr, res, 1000);
    rtMessage_Release(res);
  }

  messageReceived = 1;
}

int main(int argc, char* argv[])
{
  rtError err;
  rtConnection con;
  int sending = 0;
  int requesting = 0;
  int listening = 0;
  int numTopics = 0;
  
  char topics[MAX_TOPICS][MAX_TOPIC_LEN] = { "A.B.C", "\0", "\0", "\0", "\0" };
  char const* message = "Hello World";
  int wait = 30;
  char const* socket = "tcp://127.0.0.1:10001";

  printf("logfile=/opt/logs/rtmessage_%d.log\n", getpid());

  rtLog_SetLevel(RT_LOG_DEBUG);
  rtLogSetLogHandler(logHandler);


  while (1)
  {
    int option_index = 0;
    int c;

    static struct option long_options[] = 
    {
      {"send",        no_argument,        0, 'S' },
      {"request",     no_argument,        0, 'R' },
      {"listen",      no_argument,        0, 'L' },
      {"topic",       required_argument,  0, 't' },
      {"msg",         required_argument,  0, 'm' },
      {"wait",        required_argument,  0, 'w' },
      {"socket",      required_argument,  0, 's' },
      {"log-level",   required_argument,  0, 'l' },
      {"help",        no_argument,        0, 'h' },
      {0, 0, 0, 0}
    };

    c = getopt_long(argc, argv, "SRLt:m:w:s:l:h", long_options, &option_index);
    if (c == -1)
      break;

    switch (c)
    {
      case 'S':
        sending = 1;
        printf("Argument: Send=true\n");
        break;
      case 'R':
        requesting = 1;
        printf("Argument: Request=true\n");
        break;
      case 'L':
        listening = 1;
        printf("Argument: Listen=true\n");
        break;
      case 't':
        if(numTopics < MAX_TOPICS)
        {
            strncpy(topics[numTopics], optarg, MAX_TOPIC_LEN-1);
            topics[numTopics][255] = 0;
            printf("Argument: Topic=%s\n", topics[numTopics]);
            numTopics++;
        }
        else
        {
            printf("Argument: max supported topics %d reached.  Ignoring %s\n", MAX_TOPICS, optarg);
        }
        break;
      case 'm':
        message = optarg;
        printf("Argument: Message=%s\n", message);
        break;
      case 'w':
        wait = atoi(optarg);
        printf("Argument: Wait=%d\n", wait);
        break;
      case 'l':
        {
            rtLog_SetLevel(rtLogLevelFromString(optarg));
            printf("Argument: Log level=%s\n", rtLogLevelToString(rtLog_GetLevel()));
        }
        break;
      case 's':
        socket = optarg;
        printf("Argument: Socket=%s\n", socket);
        break;
      case 'h':
        printHelp();
        break;
      default:
        fprintf(stderr, "?? getopt returned character code 0%o ??\n\trun sample_super -h for help", c);
    }
  }

  if(!sending && !requesting && !listening)
  {
    printHelp();
    exit(0);
  }

  rtLog_Info("connecting to socket %s\n", socket);

  err = rtConnection_Create(&con, "APP1", socket);

  if(err != RT_OK)
  {
    rtLog_Error("rtConnection_Create failed with error %s trying to connect to %s. Exiting.\n", rtStrError(err), socket);
    exit(0);
  }

  if(sending)
  {
    int i;
    for(i = 0; i < numTopics; ++i)
    {
        rtMessage m;

        rtLog_Info("sending on topic %s\n", topics[i]);

        rtMessage_Create(&m);
        rtMessage_SetString(m, "msg", message);
        err = rtConnection_SendMessage(con, m, topics[i]);
        rtMessage_Release(m);
        if(err != RT_OK)
        {
          rtLog_Error("rtConnection_SendMessage failed with error %s trying to send message to topic %s. Exiting.\n", rtStrError(err), topics[i]);
          rtConnection_Destroy(con);
          exit(0);
        }
    }
  }
  
  if(requesting)
  {
    int i;
    for(i = 0; i < numTopics; ++i)
    {
        rtMessage req;
        rtMessage res;
        rtMessage_Create(&req);
        rtMessage_SetString(req, "msg", message);
        err = rtConnection_SendRequest(con, req, topics[i], &res, 2000);
        rtMessage_Release(req);
        if (err == RT_OK)
        {
          char* p = NULL;
          uint32_t len = 0;
          rtMessage_ToString(res, &p, &len);
          rtLog_Info("\tGot response::%.*s\n", len, p);
          free(p);
          rtMessage_Release(res);
        }
        else
        {
          rtLog_Error("rtConnection_SendRequest failed with error %s trying to send message to topic %s. Exiting.\n", rtStrError(err), topics[i]);
          rtConnection_Destroy(con);
          exit(0);
        }
    }
  }
  
  if(listening)
  {
    int i;
    for(i = 0; i < numTopics; ++i)
    {
        if(topics[i][0] != '\0')
        {
            rtLog_Info("listening on topic %s\n", topics[i]);
            rtConnection_AddListener(con, topics[i], onMessage, con);
        }
        else
        {
            break;
        }
    }

    while(wait > 0)
    {
      sleep(1);
      if(messageReceived)
      {
        wait = 0;
      }
    }
  }

  sleep(1);

  rtConnection_Destroy(con);

  rtLog_Info("super_sample exiting\n");  

  return 0;
}

