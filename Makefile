# Makefile for iemnet

lib.name = iemnet

class.sources = \
        tcpserver.c \
        tcpclient.c \
        tcpsend.c \
        tcpreceive.c \
        udpreceive.c \
        udpsend.c \
        udpclient.c \
        udpserver.c \
	udpsocket.c \
	$(empty)

shared.sources = \
	iemnet.c \
	iemnet_data.c \
	iemnet_receiver.c \
	iemnet_sender.c \
	$(empty)

datafiles = \
	iemnet-meta.pd \
	tcpclient-help.pd \
	tcpreceive-help.pd \
	tcpsend-help.pd \
	tcpserver-help.pd \
	udpclient-help.pd \
	udpreceive-help.pd \
	udpsend-help.pd \
	udpserver-help.pd \
	udpsndrcv-help.pd \
	udpsndrcv.pd \
	udpsocket-help.pd \
	LICENSE.md \
	README.md \
	$(empty)

iemnet.version := $(shell sed -n \
    's|^\#X text [0-9][0-9]* [0-9][0-9]* VERSION \(.*\);|\1|p' \
    iemnet-meta.pd)

cflags = -DVERSION='"$(iemnet.version)"'


# minimum supported windows version
# 0x0400 - Windows NT 4.0
# 0x0500 - Windows 2000
# 0x0501 - Windows XP
# 0x0502 - Windows Server 2003
# 0x0600 - Windows Vista
# 0x0600 - Windows Server 2008
# 0x0601 - Windows 7
# 0x0602 - Windows 8
# 0x0603 - Windows 8.1
# 0x0A00 - Windows 10

define forWindows
  # IPv6 requires Vista
  winver_cflags = -DWINVER=0x0600 -D_WIN32_WINNT=0x0600
  ldlibs = -lws2_32 -lpthread
endef

cflags += $(winver_cflags)

-include Makefile.local

# This Makefile is based on the Makefile from pd-lib-builder written by
# Katja Vetter. You can get it from:
# https://github.com/pure-data/pd-lib-builder

PDLIBBUILDER_DIR=pd-lib-builder/
include $(firstword $(wildcard $(PDLIBBUILDER_DIR)/Makefile.pdlibbuilder Makefile.pdlibbuilder))
