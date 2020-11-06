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
#include "rtConnection.h"
#include "rtEncoder.h"
#include "rtError.h"
#include "rtLog.h"
#include "rtMessageHeader.h"
#include "rtSocket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wait.h>

#define RTMSG_LISTENERS_MAX 64
#define RTMSG_SEND_BUFFER_SIZE (1024 * 8)

struct _rtListener
{
  int                     in_use;
  void*                   closure;
  char*                   expression;
  uint32_t                subscription_id;
  rtMessageCallback       callback;
};

struct _rtConnection
{
  int                     fd;
  struct sockaddr_storage local_endpoint;
  struct sockaddr_storage remote_endpoint;
  uint8_t*                send_buffer;
  uint8_t*                recv_buffer;
  uint32_t                sequence_number;
  char*                   application_name;
  rtConnectionState       state;
  char                    inbox_name[RTMSG_HEADER_MAX_TOPIC_LENGTH];
  struct _rtListener      listeners[RTMSG_LISTENERS_MAX];
  rtMessage               response;
};

static void onInboxMessage(rtMessageHeader const* hdr, uint8_t const* p, uint32_t n, void* closure)
{
  if (hdr->flags & rtMessageFlags_Response)
  {
    struct _rtConnection* con = (struct _rtConnection *) closure;
    if (con->response != NULL)
    {
      rtMessage_Release(con->response);
      con->response = NULL;
    }
    rtMessage_FromBytes(&con->response, p, n);
  }
}

static rtError rtConnection_SendInternal(rtConnection con, char const* topic,
  uint8_t const* buff, uint32_t n, char const* reply_topic, int flags);
  

static uint32_t
rtConnection_GetNextSubscriptionId()
{
  static uint32_t next_id = 1;
  return next_id++;
}

static int
rtConnection_ShouldReregister(rtError e)
{
  if (rtErrorFromErrno(ENOTCONN) == e) return 1;
  if (rtErrorFromErrno(EPIPE) == e) return 1;
  return 0;
}

static rtError
rtConnection_ConnectAndRegister(rtConnection con)
{
  int i;
  int ret;
  char buff[64];
  uint16_t port;
  socklen_t socket_length;

  i = 1;
  ret = 0;
  memset(buff, 0, sizeof(buff));
  port = 0;

  rtSocketStorage_GetLength(&con->remote_endpoint, &socket_length);

  if (con->fd != -1)
    close(con->fd);

  con->fd = socket(con->remote_endpoint.ss_family, SOCK_STREAM, 0);
  if (con->fd == -1)
    return rtErrorFromErrno(errno);

  fcntl(con->fd, F_SETFD, fcntl(con->fd, F_GETFD) | FD_CLOEXEC);
  setsockopt(con->fd, SOL_TCP, TCP_NODELAY, &i, sizeof(i));

  rtSocketStorage_ToString(&con->remote_endpoint,buff, sizeof(buff), &port);

  int retry = 0;
  while (retry <= 3)
  {
    ret = connect(con->fd, (struct sockaddr *)&con->remote_endpoint, socket_length);
    if (ret == -1)
    {
      int err = errno;
      if (err == ECONNREFUSED)
      {
        sleep(1);
        retry++;
      }
      else
      {
        sleep(1);
        rtLog_Warn("error connecting to %s:%d. %s", buff, port, strerror(err));
      }
    }
    else
    {
      break;
    }
  }

  rtSocket_GetLocalEndpoint(con->fd, &con->local_endpoint);

  {
    uint16_t local_port;
    uint16_t remote_port;
    char local_addr[64];
    char remote_addr[64];

    rtSocketStorage_ToString(&con->local_endpoint, local_addr, sizeof(local_addr), &local_port);
    rtSocketStorage_ToString(&con->remote_endpoint, remote_addr, sizeof(remote_addr), &remote_port);
    rtLog_Debug("connect %s:%d -> %s:%d", local_addr, local_port, remote_addr, remote_port);
  }

  for (i = 0; i < RTMSG_LISTENERS_MAX; ++i)
  {
    if (con->listeners[i].in_use)
    {
      rtMessage m;
      rtMessage_Create(&m);
      rtMessage_SetString(m, "topic", con->listeners[i].expression);
      rtMessage_SetInt32(m, "route_id", con->listeners[i].subscription_id);
      rtConnection_SendMessage(con, m, "_RTROUTED.INBOX.SUBSCRIBE");
      rtMessage_Release(m);
    }
  }

  return RT_OK;
}

static rtError
rtConnection_EnsureRoutingDaemon()
{
  int ret = system("/usr/bin/rtrouted 2> /dev/null");

  // 127 is return from sh -c (@see system manpage) when command is not found in $PATH
  if (WEXITSTATUS(ret) == 127)
    ret = system("/usr/bin/rtrouted 2> /dev/null");

  // exit(12) from rtrouted means another instance is already running
  if (WEXITSTATUS(ret) == 12)
    return RT_OK;

  if (ret != 0)
    rtLog_Error("Cannot run rtrouted. Code:%d", ret);

  return RT_OK;
}

static rtError
rtConnection_ReadUntil(rtConnection con, uint8_t* buff, int count, int32_t timeout)
{
  ssize_t bytes_read = 0;
  ssize_t bytes_to_read = count;

  (void) timeout;

  while (bytes_read < bytes_to_read)
  {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(con->fd, &read_fds);

    #if 0
    if (timeout != INT32_MAX)
    {
      // TODO: include suseconds_t tv_usecs;
      time_t seconds = (timeout / 1000);
      struct timeval tv = { seconds, 0 };
      select(con->fd + 1, &read_fds, NULL, NULL, &tv);
      if (!FD_ISSET(con->fd, &read_fds))
        return RT_ERROR_TIMEOUT;
    }
    #endif

    ssize_t n = recv(con->fd, buff + bytes_read, (bytes_to_read - bytes_read), MSG_NOSIGNAL);
    if (n == 0)
    {
      rtLog_Error("Failed to read error : %s", rtStrError(rtErrorFromErrno(ENOTCONN)));
      return rtErrorFromErrno(ENOTCONN);
    }

    if (n == -1)
    {
      if (errno == EINTR)
        continue;
      rtError e = rtErrorFromErrno(errno);
      rtLog_Error("failed to read from fd %d. %s", con->fd, rtStrError(e));
      return e;
    }
    bytes_read += n;
  }
  return RT_OK;
}

rtError
rtConnection_Create(rtConnection* con, char const* application_name, char const* router_config)
{
  rtMessage conf;
  rtMessage_Create(&conf);
  rtMessage_SetString(conf, "appname", application_name);
  rtMessage_SetString(conf, "uri", router_config);
  rtMessage_SetInt32(conf, "start_router", 1);
  return rtConnection_CreateWithConfig(con, conf);
}

rtError
rtConnection_CreateWithConfig(rtConnection* con, rtMessage const conf)
{
  int i;
  rtError err;
  char const* application_name;
  char const* router_config;
  int start_router;

  i = 0;
  err = RT_OK;
  application_name = NULL;
  router_config = NULL;
  start_router = 0;

  rtMessage_GetString(conf, "appname", &application_name);
  rtMessage_GetString(conf, "uri", &router_config);
  rtMessage_GetInt32(conf, "start_router", &start_router);

  if (start_router)
  {
    err = rtConnection_EnsureRoutingDaemon();
    if (err != RT_OK)
      return err;
  }

  rtConnection c = (rtConnection) malloc(sizeof(struct _rtConnection));
  if (!c)
    return rtErrorFromErrno(ENOMEM);

  for (i = 0; i < RTMSG_LISTENERS_MAX; ++i)
  {
    c->listeners[i].in_use = 0;
    c->listeners[i].closure = NULL;
    c->listeners[i].callback = NULL;
    c->listeners[i].subscription_id = 0;
  }

  c->response = NULL;
  c->send_buffer = (uint8_t *) malloc(RTMSG_SEND_BUFFER_SIZE);
  c->recv_buffer = (uint8_t *) malloc(RTMSG_SEND_BUFFER_SIZE);
  c->sequence_number = 1;
  c->application_name = strdup(application_name);
  c->fd = -1;
  memset(c->inbox_name, 0, RTMSG_HEADER_MAX_TOPIC_LENGTH);
  memset(&c->local_endpoint, 0, sizeof(struct sockaddr_storage));
  memset(&c->remote_endpoint, 0, sizeof(struct sockaddr_storage));
  memset(c->send_buffer, 0, RTMSG_SEND_BUFFER_SIZE);
  memset(c->recv_buffer, 0, RTMSG_SEND_BUFFER_SIZE);
  snprintf(c->inbox_name, RTMSG_HEADER_MAX_TOPIC_LENGTH, "%s.INBOX.%d", c->application_name, (int) getpid());

  err = rtSocketStorage_FromString(&c->remote_endpoint, router_config);
  if (err != RT_OK)
  {
    rtLog_Warn("failed to parse:%s. %s", router_config, rtStrError(err));
    free(c);
    return err;
  }

  err = rtConnection_ConnectAndRegister(c);
  if (err != RT_OK)
  {
  }

  if (err == RT_OK)
  {
    rtConnection_AddListener(c, c->inbox_name, onInboxMessage, c);
    *con = c;
  }

  return err;
}

rtError
rtConnection_Destroy(rtConnection con)
{
  if (con)
  {
    if (con->fd != -1)
    {
      shutdown(con->fd, SHUT_RDWR);
      close(con->fd);
    }
    if (con->send_buffer)
      free(con->send_buffer);
    if (con->recv_buffer)
      free(con->recv_buffer);
    if (con->application_name)
      free(con->application_name);
    free(con);
  }
  return 0;
}

rtError
rtConnection_SendErrorMessageToCaller(int clnt_fd ,rtMessageHeader const* request_header)
{
    rtConnection t_con = (rtConnection) malloc(sizeof(struct _rtConnection));
    memset(t_con,0,sizeof(struct _rtConnection));
 
    t_con->fd = clnt_fd;
    t_con->send_buffer = (uint8_t *) malloc(RTMSG_SEND_BUFFER_SIZE);
    t_con->recv_buffer = (uint8_t *) malloc(RTMSG_SEND_BUFFER_SIZE);
    memset(t_con->send_buffer, 0, RTMSG_SEND_BUFFER_SIZE);
    memset(t_con->recv_buffer, 0, RTMSG_SEND_BUFFER_SIZE);
    //Adding topic in request header
    rtMessageHeader new_header;
    rtMessageHeader_Init(&new_header);
    strcpy(new_header.topic, "NO.ROUTE.RESPONSE");
    strcpy(new_header.reply_topic, request_header->reply_topic);

    //Create Response
    rtMessage res;
    rtMessage_Create(&res);
    rtMessage msg;
    rtMessage_Create(&msg);
    rtMessage_SetString(msg, "name", " ");
    rtMessage_SetString(msg, "value", " ");
    rtMessage_SetString(msg, "status_msg", "No Route found for this Parameter");
    rtMessage_SetInt32(msg, "status", 1);
    rtMessage_AddMessage(res, "result", msg);
    //Send response
    rtConnection_SendResponse(t_con, &new_header, res, 1000);
    rtMessage_Release(msg);
    rtMessage_Release(res);
    return RT_OK;
}
rtError
rtConnection_SendMessage(rtConnection con, rtMessage msg, char const* topic)
{
  uint8_t* p;
  uint32_t n;
  rtError err;
  rtMessage_ToByteArray(msg, &p, &n);
  err = rtConnection_SendBinary(con, topic, p, n);
  free(p);
  return err;
}

rtError
rtConnection_SendResponse(rtConnection con, rtMessageHeader const* request_hdr, rtMessage const res, int32_t timeout)
{
  uint8_t* p;
  uint32_t n;
  rtError err;

  rtMessage_ToByteArray(res, &p, &n);
  err = rtConnection_SendInternal(con, request_hdr->reply_topic, p, n, request_hdr->topic, rtMessageFlags_Response);
  free(p);

  (void) timeout;

  return err;
}

rtError
rtConnection_SendBinary(rtConnection con, char const* topic, uint8_t const* p, uint32_t n)
{
  return rtConnection_SendInternal(con, topic, p, n, NULL, 0);
}

rtError
rtConnection_SendRequest(rtConnection con, rtMessage const req, char const* topic,
  rtMessage* res, int32_t timeout)
{
  uint8_t* p;
  uint32_t n;
  rtError err;
  rtMessage_ToByteArray(req, &p, &n);

  err = rtConnection_SendInternal(con, topic, p, n, con->inbox_name, rtMessageFlags_Request);
  free(p);
  if (err != RT_OK)
    return err;

  *res = NULL;

  time_t start = time(NULL);
  time_t now   = start;
  time_t duration = (timeout / 1000);

  while ((now - start) < duration)
  {
    err = rtConnection_TimedDispatch(con, timeout);
    if (err != RT_ERROR_TIMEOUT && err != RT_OK)
      return err;

    if (con->response != NULL)
    {
      // TODO: add ref counting to rtMessage
      *res = con->response;
      con->response = NULL;
      return RT_OK;
    }

    now = time(NULL);
  }

  return RT_ERROR_TIMEOUT;
}

rtError
rtConnection_SendInternal(rtConnection con, char const* topic, uint8_t const* buff,
  uint32_t n, char const* reply_topic, int flags)
{
  rtError err;
  int num_attempts;
  int max_attempts;
  ssize_t bytes_sent;
  rtMessageHeader header;

  max_attempts = 2;
  num_attempts = 0;

  rtMessageHeader_Init(&header);
  header.payload_length = n;

  strncpy(header.topic, topic, sizeof(header.topic)-1);
  header.topic_length = strlen(header.topic);
  if (reply_topic)
  {
    strncpy(header.reply_topic, reply_topic, sizeof(header.reply_topic)-1);
    header.reply_topic_length = strlen(reply_topic);
  }
  else
  {
    header.reply_topic[0] = '\0';
    header.reply_topic_length = 0;
  }
  header.sequence_number = con->sequence_number++;
  header.flags = flags;

  err = rtMessageHeader_Encode(&header, con->send_buffer);
  if (err != RT_OK)
    return err;

  do
  {
    bytes_sent = send(con->fd, con->send_buffer, header.header_length, MSG_NOSIGNAL);
    if (bytes_sent != header.header_length)
    {
      if (bytes_sent == -1)
        err = rtErrorFromErrno(errno);
      else
        err = RT_FAIL;
    }

    if (err == RT_OK)
    {
      bytes_sent = send(con->fd, buff, header.payload_length, MSG_NOSIGNAL);
      if (bytes_sent != header.payload_length)
      {
        if (bytes_sent == -1)
          err = rtErrorFromErrno(errno);
        else
          err = RT_FAIL;
      }
    }

    if (err != RT_OK && rtConnection_ShouldReregister(err))
    {
      err = rtConnection_EnsureRoutingDaemon();
      if (err == RT_OK)
        err = rtConnection_ConnectAndRegister(con);
    }
  }
  while ((err != RT_OK) && (num_attempts++ < max_attempts));

  return err;
}

rtError
rtConnection_AddListener(rtConnection con, char const* expression, rtMessageCallback callback, void* closure)
{
  int i;

  for (i = 0; i < RTMSG_LISTENERS_MAX; ++i)
  {
    if (!con->listeners[i].in_use)
      break;
  }

  if (i >= RTMSG_LISTENERS_MAX)
    return rtErrorFromErrno(ENOMEM);

  con->listeners[i].in_use = 1;
  con->listeners[i].subscription_id = rtConnection_GetNextSubscriptionId();
  con->listeners[i].closure = closure;
  con->listeners[i].callback = callback;
  con->listeners[i].expression = strdup(expression);

  rtMessage m;
  rtMessage_Create(&m);
  rtMessage_SetString(m, "topic", expression);
  rtMessage_SetInt32(m, "route_id", con->listeners[i].subscription_id); 
  rtConnection_SendMessage(con, m, "_RTROUTED.INBOX.SUBSCRIBE");
  rtMessage_Release(m);

  return 0;
}

rtError
rtConnection_Dispatch(rtConnection con)
{
  return rtConnection_TimedDispatch(con, -1);
}

rtError
rtConnection_TimedDispatch(rtConnection con, int32_t timeout)
{
  int i;
  int num_attempts;
  int max_attempts;
  uint8_t const*  itr;
  rtMessageHeader hdr;
  rtError err;

  i = 0;
  num_attempts = 0;
  max_attempts = 4;

  rtMessageHeader_Init(&hdr);

  // TODO: no error handling right now, all synch I/O

  do
  {
    con->state = rtConnectionState_ReadHeaderPreamble;
    err = rtConnection_ReadUntil(con, con->recv_buffer, 4, timeout);

    if (err == RT_ERROR_TIMEOUT)
      return err;

    if (err == RT_OK)
    {
      itr = &con->recv_buffer[2];
      rtEncoder_DecodeUInt16(&itr, &hdr.header_length);
      err = rtConnection_ReadUntil(con, con->recv_buffer + 4, (hdr.header_length-4), timeout);
    }

    if (err == RT_OK)
    {
      #if 0
      int i;
      for (i = 0; i < hdr.header_length; ++i)
      {
        if (i %16 == 0)
          printf("\n");
        printf("0x%02x ", con->recv_buffer[i]);
      }
      printf("\n\n\n");
      #endif
      err = rtMessageHeader_Decode(&hdr, con->recv_buffer);
    }

    if (err == RT_OK)
    {
      err = rtConnection_ReadUntil(con, con->recv_buffer + hdr.header_length, hdr.payload_length, timeout);
      if (err == RT_OK)
      {
        // help out json parsers and other string parses
        con->recv_buffer[hdr.header_length + hdr.payload_length] = '\0';
      }
    }

    if (err != RT_OK && rtConnection_ShouldReregister(err))
    {
      err = rtConnection_EnsureRoutingDaemon();
      if (err == RT_OK)
        err = rtConnection_ConnectAndRegister(con);
    }
  }
  while ((err != RT_OK) && (num_attempts++ < max_attempts));

  if (err == RT_OK)
  {
    for (i = 0; i < RTMSG_LISTENERS_MAX; ++i)
    {
      if(strstr(hdr.reply_topic,"NO.ROUTE.RESPONSE")) {
        con->listeners[i].subscription_id = 0;
      }
      if (con->listeners[i].in_use && (con->listeners[i].subscription_id == hdr.control_data))
      {
        rtLog_Debug("found subscription match:%d", i);
        break;
      }
    }

    if (i < RTMSG_LISTENERS_MAX)
    {
      con->listeners[i].callback(&hdr, con->recv_buffer + hdr.header_length, hdr.payload_length,
        con->listeners[i].closure);
    }
  }

  return RT_OK;
}
