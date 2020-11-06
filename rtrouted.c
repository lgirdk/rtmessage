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
#include "rtMessage.h"
#include "rtDebug.h"
#include "rtLog.h"
#include "rtEncoder.h"
#include "rtError.h"
#include "rtMessageHeader.h"
#include "rtSocket.h"
#include "rtVector.h"
#include "rtConnection.h"

#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <cJSON.h>

#ifdef INCLUDE_BREAKPAD
#include "breakpadwrap.h"
#endif

#define RTMSG_MAX_CONNECTED_CLIENTS 64
#define RTMSG_CLIENT_MAX_TOPICS 64
#define RTMSG_CLIENT_READ_BUFFER_SIZE (1024 * 8)
#define RTMSG_INVALID_FD -1
#define RTMSG_MAX_EXPRESSION_LEN 128
#define RTMSG_ADDR_MAX 128

typedef struct
{
  int                       fd;
  struct sockaddr_storage   endpoint;
  char                      ident[RTMSG_ADDR_MAX];
  uint8_t*                  read_buffer;
  uint8_t*                  send_buffer;
  rtConnectionState         state;
  int                       bytes_read;
  int                       bytes_to_read;
  rtMessageHeader           header;
} rtConnectedClient;

typedef struct
{
  uint32_t id;
  rtConnectedClient* client;
} rtSubscription;

typedef rtError (*rtRouteMessageHandler)(rtConnectedClient* sender, rtMessageHeader* hdr,
  uint8_t const* buff, int n, rtSubscription* subscription);

typedef struct
{
  rtSubscription*       subscription;
  rtRouteMessageHandler message_handler;
  char                  expression[RTMSG_MAX_EXPRESSION_LEN];
} rtRouteEntry;

typedef struct
{
  int fd;
  struct sockaddr_storage local_endpoint;
} rtListener;

rtVector clients;
rtVector listeners;
rtVector routes;
//rtListener        listeners[RTMSG_MAX_LISTENERS];
//rtRouteEntry      routes[RTMSG_MAX_ROUTES];

static rtError
rtRouted_BindListener(char const* socket_name, int no_delay);

static void
rtRouted_PrintHelp()
{
  printf("rtrouted [OPTIONS]...\n");
  printf("\t-f, --foreground          Run in foreground\n");
  printf("\t-d, --no-delay            Enabled debugging\n");
  printf("\t-l, --log-level <level>   Change logging level\n");
  printf("\t-r, --debug-route         Add a catch all route that dumps messages to stdout\n");
  printf("\t-s, --socket              [tcp://ip:port unix:///path/to/domain_socket]\n");
  printf("\t-h, --help                Print this help\n");
  exit(0);
}

static rtError
rtRouted_AddRoute(rtRouteMessageHandler handler, char const* exp, rtSubscription* subscription)
{
  rtRouteEntry* route = (rtRouteEntry *) malloc(sizeof(rtRouteEntry));
  route->subscription = subscription;
  route->message_handler = handler;
  strncpy(route->expression, exp, RTMSG_MAX_EXPRESSION_LEN);
  rtVector_PushBack(routes, route);
  rtLog_Debug("client [%s] added new route:%s", subscription->client->ident, exp);
  return RT_OK;
}

static int
rtRouted_FileExists(char const* s)
{
  int ret;
  struct stat buf;

  if (!s || strlen(s) == 0) return 0;

  memset(&buf, 0, sizeof(buf));
  ret = stat(s, &buf);

  return ret == 0 ? 1 : 0;
}

static rtError
rtRouted_ParseConfig(char const* fname)
{
  int       i;
  int       n;
  char*     buff;
  FILE*     fin;
  cJSON*    json;
  cJSON*    listeners;

  if (!fname || strlen(fname) == 0)
  {
    rtLog_Warn("cannot prase empty configuration file");
    return RT_OK;
  }

  if (!rtRouted_FileExists(fname))
  {
    rtLog_Error("configuration file %s doesn't exist", fname);
    return RT_OK;
  }

  n = 0;
  buff = NULL;
  fin = NULL;
  json = NULL;

  // take easy way out. this is used to store contents of configuration file
  buff = (char *) malloc(8192);
  buff[0] = '\0';


  rtLog_Debug("parsing configuration from file %s", fname);

  fin = fopen(fname, "r");
  if (fin)
  {
    char temp[256];
    while (fgets(temp, sizeof(temp), fin))
      strncat(buff, temp, strlen(temp));
    fclose(fin);
  }
  else
  {
    int err = errno;
    rtLog_Error("failed to open configuration file %s. %s", fname, strerror(errno));
    exit(1);
  }

  json = cJSON_Parse(buff);
  free(buff);

  if (!json)
  {
    rtLog_Error("error parising configuration file");

    char const* p = cJSON_GetErrorPtr();
    if (p)
    {
      char const* end = (buff + strlen(buff));
      int n = (int) (end - p);
      if (n > 64)
        n = 64;
      rtLog_Error("%.*s\n", n, p);
    }

    exit(1);
  }
  else
  {
    listeners = cJSON_GetObjectItem(json, "listeners");
    if (listeners)
    {
      for (i = 0, n = cJSON_GetArraySize(listeners); i < n; ++i)
      {
        cJSON* item = cJSON_GetArrayItem(listeners, i);
        if (item)
        {
          cJSON* uri = cJSON_GetObjectItem(item, "uri");
          if (uri)
            rtRouted_BindListener(uri->valuestring, 1);
        }
      }
    }
    cJSON_Delete(json);
  }
}

static rtError
rtRouted_ClearClientRoutes(rtConnectedClient* clnt)
{
  size_t i;
  for (i = 0; i < rtVector_Size(routes);)
  {
    rtRouteEntry* route = (rtRouteEntry *) rtVector_At(routes, i);
    if (route->subscription && route->subscription->client == clnt)
    {
      rtVector_RemoveItem(routes, route, NULL);
      free(route->subscription);
      free(route);
    }
    else
    {
      i++;
    }
  }

  return RT_OK;
}

static void
rtConnectedClient_Destroy(rtConnectedClient* clnt)
{
  rtRouted_ClearClientRoutes(clnt);

  if (clnt->fd != -1)
    close(clnt->fd);

  if (clnt->read_buffer)
    free(clnt->read_buffer);

  if (clnt->send_buffer)
    free(clnt->send_buffer);

  free(clnt);
}

static rtError
rtRouted_ForwardMessage(rtConnectedClient* sender, rtMessageHeader* hdr, uint8_t const* buff, int n, rtSubscription* subscription)
{
  ssize_t bytes_sent;

  (void) sender;

  rtMessageHeader new_header;
  rtMessageHeader_Init(&new_header);
  new_header.version = hdr->version;
  new_header.header_length = hdr->header_length;
  new_header.sequence_number = hdr->sequence_number;
  new_header.control_data = subscription->id;
  new_header.payload_length = hdr->payload_length;
  new_header.topic_length = hdr->topic_length;
  new_header.reply_topic_length = hdr->reply_topic_length;
  new_header.flags = hdr->flags;
  strcpy(new_header.topic, hdr->topic);
  strcpy(new_header.reply_topic, hdr->reply_topic);
  rtMessageHeader_Encode(&new_header, subscription->client->send_buffer);

  // rtDebug_PrintBuffer("fwd header", subscription->client->send_buffer, new_header.length);

  bytes_sent = send(subscription->client->fd, subscription->client->send_buffer, new_header.header_length, MSG_NOSIGNAL);
  if (bytes_sent == -1)
  {
    if (errno == EBADF)
    {
      return rtErrorFromErrno(errno);
    }
    else
    {
      rtLog_Warn("error forwarding message to client. %d %s", errno, strerror(errno));
    }
    return RT_FAIL;
  }

  bytes_sent = send(subscription->client->fd, buff, n, MSG_NOSIGNAL);
  if (bytes_sent == -1)
  {
    if (errno == EBADF)
    {
      return rtErrorFromErrno(EBADF);
    }
    else
    {
      rtLog_Warn("error forwarding message to client. %d %s", errno, strerror(errno));
    }
    return RT_FAIL;
  }
  return RT_OK;
}

static rtError 
rtRouted_PrintMessage(rtConnectedClient* sender, rtMessageHeader* hdr, uint8_t const* buff,
  int n, rtSubscription* subscription)
{
  (void) hdr;
  (void) sender;
  (void) subscription;

  printf("message debug\n");
  printf("'%.*s'\n", n, (char *) buff);
  return RT_OK;
}

static rtError
rtRouted_OnMessage(rtConnectedClient* sender, rtMessageHeader* hdr, uint8_t const* buff,
  int n, rtSubscription* not_unsed)
{
  (void) not_unsed;

  if (strcmp(hdr->topic, "_RTROUTED.INBOX.SUBSCRIBE") == 0)
  {
    char const* expression = NULL;
    int32_t route_id = 0;

    rtMessage m;
    rtMessage_FromBytes(&m, buff, n);
    rtMessage_GetString(m, "topic", &expression);
    rtMessage_GetInt32(m, "route_id", &route_id);

    rtSubscription* subscription = (rtSubscription *) malloc(sizeof(rtSubscription));
    subscription->id = route_id;
    subscription->client = sender;
    rtRouted_AddRoute(rtRouted_ForwardMessage, expression, subscription);

    rtMessage_Release(m);
  }
  else if (strcmp(hdr->topic, "_RTROUTED.INBOX.HELLO") == 0)
  {
    char const* inbox = NULL;

    rtMessage m;
    rtMessage_FromBytes(&m, buff, n);
    rtMessage_GetString(m, "inbox", &inbox);

    rtSubscription* subscription = (rtSubscription *) malloc(sizeof(rtSubscription));
    subscription->id = 0;
    subscription->client = sender;
    rtRouted_AddRoute(rtRouted_ForwardMessage, inbox, subscription);

    rtMessage_Release(m);
  }
  else
  {
    rtLog_Debug("no handler for message:%s", hdr->topic);
  }
  return RT_OK;
}

static int
rtRouted_IsTopicMatch(char const* topic, char const* exp)
{
  char const* t = topic;
  char const* e = exp;


  while (*t && *e)
  {
    if (*e == '*')
    {
      while (*t && *t != '.')
        t++;
      e++;
    }

    if (*e == '>')
    {
      while (*t)
        t++;
      e++;
    }

    if (!(*t || *e))
      break;

    if (*t != *e)
      break;

    t++;
    e++;
  }

  // rtLogInfo("match[%d]: %s <> %s", !(*t || *e), topic, exp);
  return !(*t || *e);
}

static void
rtConnectedClient_Init(rtConnectedClient* clnt, int fd, struct sockaddr_storage* remote_endpoint)
{
  clnt->fd = fd;
  clnt->state = rtConnectionState_ReadHeaderPreamble;
  clnt->bytes_read = 0;
  clnt->bytes_to_read = 4;
  clnt->read_buffer = (uint8_t *) malloc(RTMSG_CLIENT_READ_BUFFER_SIZE);
  clnt->send_buffer = (uint8_t *) malloc(RTMSG_CLIENT_READ_BUFFER_SIZE);
  memcpy(&clnt->endpoint, remote_endpoint, sizeof(struct sockaddr_storage));
  memset(clnt->read_buffer, 0, RTMSG_CLIENT_READ_BUFFER_SIZE);
  memset(clnt->send_buffer, 0, RTMSG_CLIENT_READ_BUFFER_SIZE);
  rtMessageHeader_Init(&clnt->header);
}

static void
rtRouter_DispatchMessageFromClient(rtConnectedClient* clnt)
{
  size_t i;
  size_t n;
  int match_found = 0;

  for (i = 0, n = rtVector_Size(routes); i < n;)
  {
    rtError err = RT_OK;
    rtRouteEntry* route = (rtRouteEntry *) rtVector_At(routes, i);
    if (rtRouted_IsTopicMatch(clnt->header.topic, route->expression))
    {
      match_found = 1;
      err = route->message_handler(clnt, &clnt->header, clnt->read_buffer +
          clnt->header.header_length, clnt->header.payload_length, route->subscription);

      if (err != RT_OK)
      {
        if (err == rtErrorFromErrno(EBADF))
        {
          rtRouted_ClearClientRoutes(clnt);
          n = rtVector_Size(routes);
        }
      }
    }

    if (err == RT_OK)
      i++;
  }

  int is_request = rtMessageHeader_IsRequest(&clnt->header);
  if (!match_found && is_request)
  {
    // TODO: If this is a request, then send message directly back
    // to caller
    rtLog_Error("no client found for match:%s", clnt->header.topic);
    //No route Found , Returning a Error Message to caller
    rtConnection_SendErrorMessageToCaller(clnt->fd, &clnt->header);
  }
}

static rtError
rtConnectedClient_Read(rtConnectedClient* clnt)
{
  ssize_t bytes_read;
  int bytes_to_read = (clnt->bytes_to_read - clnt->bytes_read);

  bytes_read = recv(clnt->fd, &clnt->read_buffer[clnt->bytes_read], bytes_to_read, MSG_NOSIGNAL);
  if (bytes_read == -1)
  {
    rtError e = rtErrorFromErrno(errno);
    rtLog_Warn("read:%s", rtStrError(e));
    return e;
  }

  if (bytes_read == 0)
  {
    rtLog_Debug("read zero bytes, stream closed");
    return RT_ERROR_STREAM_CLOSED;
  }

  clnt->bytes_read += bytes_read;

  switch (clnt->state)
  {
    case rtConnectionState_ReadHeaderPreamble:
    {
      // read version/length of header
      if (clnt->bytes_read == clnt->bytes_to_read)
      {
        uint8_t const* itr = &clnt->read_buffer[2];
        uint16_t header_length = 0;
        rtEncoder_DecodeUInt16(&itr, &header_length);
        clnt->bytes_to_read += (header_length - 4);
        clnt->state = rtConnectionState_ReadHeader;
      }
    }
    break;

    case rtConnectionState_ReadHeader:
    {
      if (clnt->bytes_read == clnt->bytes_to_read)
      {
        rtMessageHeader_Decode(&clnt->header, clnt->read_buffer);
        clnt->bytes_to_read += clnt->header.payload_length;
        clnt->state = rtConnectionState_ReadPayload;
      }
    }
    break;

    case rtConnectionState_ReadPayload:
    {
      if (clnt->bytes_read == clnt->bytes_to_read)
      {
        rtRouter_DispatchMessageFromClient(clnt);
        clnt->bytes_to_read = 4;
        clnt->bytes_read = 0;
        clnt->state = rtConnectionState_ReadHeaderPreamble;
        rtMessageHeader_Init(&clnt->header);
      }
    }
    break;
  }

  return RT_OK;
}

static void
rtRouted_PushFd(fd_set* fds, int fd, int* maxFd)
{
  if (fd != RTMSG_INVALID_FD)
  {
    FD_SET(fd, fds);
    if (maxFd && fd > *maxFd)
      *maxFd = fd;
  }
}

static void
rtRouted_RegisterNewClient(int fd, struct sockaddr_storage* remote_endpoint)
{
  char remote_address[64];
  uint16_t remote_port;
  rtConnectedClient* new_client;

  remote_address[0] = '\0';
  remote_port = 0;
  new_client = (rtConnectedClient *) malloc(sizeof(rtConnectedClient));
  new_client->fd = -1;

  rtConnectedClient_Init(new_client, fd, remote_endpoint);
  rtSocketStorage_ToString(&new_client->endpoint, remote_address, sizeof(remote_address), &remote_port);
  snprintf(new_client->ident, RTMSG_ADDR_MAX, "%s:%d/%d", remote_address, remote_port, fd);
  rtVector_PushBack(clients, new_client);

  rtLog_Debug("new client:%s", new_client->ident);
}

static void
rtRouted_AcceptClientConnection(rtListener* listener)
{
  int                       fd;
  socklen_t                 socket_length;
  struct sockaddr_storage   remote_endpoint;

  socket_length = sizeof(struct sockaddr_storage);
  memset(&remote_endpoint, 0, sizeof(struct sockaddr_storage));

  fd = accept(listener->fd, (struct sockaddr *)&remote_endpoint, &socket_length);
  if (fd == -1)
  {
    rtLog_Warn("accept:%s", rtStrError(errno));
    return;
  }

  rtRouted_RegisterNewClient(fd, &remote_endpoint);
}

rtError
rtRouted_BindListener(char const* socket_name, int no_delay)
{
  int ret;
  rtError err;
  socklen_t socket_length;
  rtListener* listener;

  listener = (rtListener *) malloc(sizeof(rtListener));
  listener->fd = -1;
  memset(&listener->local_endpoint, 0, sizeof(struct sockaddr_storage));

  err = rtSocketStorage_FromString(&listener->local_endpoint, socket_name);
  if (err != RT_OK)
    return err;

  rtLog_Debug("binding listener:%s", socket_name);

  listener->fd = socket(listener->local_endpoint.ss_family, SOCK_STREAM, 0);
  if (listener->fd == -1)
  {
    rtLog_Fatal("socket:%s", rtStrError(errno));
    exit(1);
  }

  rtSocketStorage_GetLength(&listener->local_endpoint, &socket_length);

  if (listener->local_endpoint.ss_family != AF_UNIX)
  {
    uint32_t one = 1;
    if (no_delay)
      setsockopt(listener->fd, SOL_TCP, TCP_NODELAY, &one, sizeof(one));

    ret = setsockopt(listener->fd, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one));
  }

  ret = bind(listener->fd, (struct sockaddr *)&listener->local_endpoint, socket_length);
  if (ret == -1)
  {
    rtError err = rtErrorFromErrno(errno);
    rtLog_Warn("failed to bind socket. %s", rtStrError(err));
    exit(1);
  }

  ret = listen(listener->fd, 4);
  if (ret == -1)
  {
    rtLog_Warn("failed to set socket to listen mode. %s", rtStrError(errno));
    exit(1);
  }

  rtVector_PushBack(listeners, listener);
  return RT_OK;
}

int main(int argc, char* argv[])
{
  int c;
  int i;
  int run_in_foreground;
  int use_no_delay;
  int ret;
  char const* socket_name;
  char const* config_file;
  rtRouteEntry* route;

  run_in_foreground = 0;
  use_no_delay = 0;
//  socket_name = "tcp://127.0.0.1:10001";
  socket_name = NULL;
  config_file = "/etc/rtrouted.conf";
  
#ifdef INCLUDE_BREAKPAD
  sleep(1);
  BreakPadWrapExceptionHandler eh;
  eh = newBreakPadWrapExceptionHandler();
#endif

  rtLog_SetLevel(RT_LOG_INFO);
  rtVector_Create(&clients);
  rtVector_Create(&listeners);
  rtVector_Create(&routes);

  FILE* pid_file = fopen("/tmp/rtrouted.pid", "w");
  if (!pid_file)
  {
    printf("failed to open pid file. %s\n", strerror(errno));
    return 0;
  }
  
  int fd = fileno(pid_file);
  int retval = flock(fd, LOCK_EX | LOCK_NB);
  if (retval != 0 && errno == EWOULDBLOCK)
  {
    rtLog_Warn("another instance of rtrouted is already running");
    exit(12);
  }

  rtLogSetLogHandler(NULL);

  // add internal route
  {
    route = (rtRouteEntry *) malloc(sizeof(rtRouteEntry));
    route->subscription = NULL;
    strcpy(route->expression, "_RTROUTED.>");
    route->message_handler = rtRouted_OnMessage;
    rtVector_PushBack(routes, route);
  }

  while (1)
  {
    int option_index = 0;
    static struct option long_options[] = 
    {
      {"foreground",  no_argument,        0, 'f'},
      {"no-delay",    no_argument,        0, 'd' },
      {"log-level",   required_argument,  0, 'l' },
      {"debug-route", no_argument,        0, 'r' },
      {"socket",      required_argument,  0, 's' },
      { "config",     required_argument,  0, 'c' },
      { "help",       no_argument,        0, 'h' },
      {0, 0, 0, 0}
    };

    c = getopt_long(argc, argv, "c:dfl:rhs:", long_options, &option_index);
    if (c == -1)
      break;

    switch (c)
    {
      case 'c':
        config_file = optarg;
        break;
      case 's':
        socket_name = optarg;
        break;
      case 'd':
        use_no_delay = 0;
        break;
      case 'f':
        run_in_foreground = 1;
        break;
      case 'l':
        rtLog_SetLevel(rtLogLevelFromString(optarg));
        break;
      case 'h':
        rtRouted_PrintHelp();
        break;
      case 'r':
      {
        route = (rtRouteEntry *) malloc(sizeof(rtRouteEntry));
        route->subscription = NULL;
        route->message_handler = &rtRouted_PrintMessage;
        strcpy(route->expression, ">");
        rtVector_PushBack(routes, route);
      }
      case '?':
        break;
      default:
        fprintf(stderr, "?? getopt returned character code 0%o ??\n", c);
    }
  }

  if (!run_in_foreground)
  {
    ret = daemon(0 /*chdir to "/"*/, 1 /*redirect stdout/stderr to /dev/null*/ );
    if (ret == -1)
    {
      rtLog_Fatal("failed to fork off daemon. %s", rtStrError(errno));
      exit(1);
    }
  }
  else
  {
    rtLog_Debug("running in foreground");
  }

  if (socket_name)
    rtRouted_BindListener(socket_name, use_no_delay);

  if (config_file)
    rtRouted_ParseConfig(config_file);

  while (1)
  {
    int n;
    int                         max_fd;
    fd_set                      read_fds;
    fd_set                      err_fds;
    struct timeval              timeout;

    max_fd= -1;
    FD_ZERO(&read_fds);
    FD_ZERO(&err_fds);
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    for (i = 0, n = rtVector_Size(listeners); i < n; ++i)
    {
      rtListener* listener = (rtListener *) rtVector_At(listeners, i);
      if (listener)
      {
        rtRouted_PushFd(&read_fds, listener->fd, &max_fd);
        rtRouted_PushFd(&err_fds, listener->fd, &max_fd);
      }
    }

    for (i = 0, n = rtVector_Size(clients); i < n; ++i)
    {
      rtConnectedClient* clnt = (rtConnectedClient *) rtVector_At(clients, i);
      if (clnt)
      {
        rtRouted_PushFd(&read_fds, clnt->fd, &max_fd);
        rtRouted_PushFd(&err_fds, clnt->fd, &max_fd);
      }
    }

    ret = select(max_fd + 1, &read_fds, NULL, &err_fds, &timeout);
    if (ret == 0)
      continue;

    if (ret == -1)
    {
      rtLog_Warn("select:%s", rtStrError(errno));
      continue;
    }

    for (i = 0, n = rtVector_Size(listeners); i < n; ++i)
    {
      rtListener* listener = (rtListener *) rtVector_At(listeners, i);
      if (FD_ISSET(listener->fd, &read_fds))
        rtRouted_AcceptClientConnection(listener);
    }

    for (i = 0, n = rtVector_Size(clients); i < n;)
    {
      rtConnectedClient* clnt = (rtConnectedClient *) rtVector_At(clients, i);
      if (FD_ISSET(clnt->fd, &read_fds))
      {
        rtError err = rtConnectedClient_Read(clnt);
        if (err != RT_OK)
        {
          rtVector_RemoveItem(clients, clnt, NULL);
          rtConnectedClient_Destroy(clnt);
          n--;
          continue;
        }
      }
      i++;
    }
  }

  rtVector_Destroy(listeners, NULL);
  rtVector_Destroy(clients, NULL);

  return 0;
}
