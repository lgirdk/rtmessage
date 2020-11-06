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
all: librtMessaging.so rtrouted sample_send sample_recv rtsub

CC=gcc

RTMSG_SRCS=\
  rtLog.c \
  rtBuffer.c \
  rtError.c \
  rtEncoder.c \
  rtSocket.c \
  rtConnection.c \
  rtMessageHeader.c \
  rtDebug.c \
  rtMessage.c

RTROUTER_SRCS=rtrouted.c

ifeq ($V, 1)
  CC_PRETTY = $(CC)
  LD_PRETTY = $(CC)
  CC_PRETTY = $(CC)
  BUILD_CC_PRETTY = $(BUILD_CC)
else
  LD_PRETTY = @echo "[LINK] $@" ; $(CXX)
  CC_PRETTY = @echo " [CC] $<" ; $(CC)
  BUILD_CC_PRETTY = @echo " [CC] $<" ; $(BUILD_CXX)
endif

ifeq ($(DEBUG), 1)
  CFLAGS += -g -fno-inline -O0 -DRT_DEBUG -IcJSON
else
  CFLAGS += -O2
endif

ifeq ($(PROFILE), 1)
  CFLAGS += -pg
endif

CFLAGS+=-Werror -Wall -Wextra -DRT_PLATFORM_LINUX -I. -fPIC
LDFLAGS=-L. -pthread
OBJDIR=obj

RTMSG_OBJS:=$(patsubst %.c, %.o, $(notdir $(RTMSG_SRCS)))
RTMSG_OBJS:=$(patsubst %, $(OBJDIR)/%, $(RTMSG_OBJS))

$(OBJDIR)/%.o : %.c
	@[ -d $(OBJDIR) ] || mkdir -p $(OBJDIR)
	$(CC_PRETTY) -c $(CFLAGS) $< -o $@

debug:
	@echo $(OBJS)

clean:
	rm -rf obj *.o librtMessaging.so
	rm -f rtrouted
	rm -f sample_send sample_recv

librtMessaging.so: $(RTMSG_OBJS)
	$(CC_PRETTY) $(RTMSG_OBJS) $(LDFLAGS) -LcJSON -lcjson -shared -o $@

rtrouted: rtrouted.c
	$(CC_PRETTY) $(CFLAGS) rtrouted.c -o rtrouted -L. -lrtMessaging -LcJSON -lcjson

sample_send: librtMessaging.so sample_send.c
	$(CC_PRETTY) $(CFLAGS) sample_send.c -L. -lrtMessaging -o sample_send -LcJSON -lcjson

sample_recv: librtMessaging.so sample_recv.c
	$(CC_PRETTY) $(CFLAGS) sample_recv.c -L. -lrtMessaging -o sample_recv -LcJSON -lcjson

rtsub: librtMessaging.so rtsub.c
	$(CC_PRETTY) $(CFLAGS) rtsub.c -L. -lrtMessaging -o rtsub -LcJSON -lcjson
