# Makefile for iemnet

lib.name = iemnet
lib.setup.sources = iemnet_setup.c

class.sources = \
        tcpserver.c \
        tcpclient.c \
        tcpsend.c \
        tcpreceive.c \
        udpreceive.c \
        udpsend.c \
        udpclient.c \
        udpserver.c \
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
	LICENSE.md \
	README.md \
	$(empty)

iemnet.version := $(shell sed -n \
    's|^\#X text [0-9][0-9]* [0-9][0-9]* VERSION \(.*\);|\1|p' \
    iemnet-meta.pd)

cflags = -DVERSION='"$(iemnet.version)"'

define forWindows
  ldlibs = -lwsock32 -lpthread
endef

-include Makefile.local

# This Makefile is based on the Makefile from pd-lib-builder written by
# Katja Vetter. You can get it from:
# https://github.com/pure-data/pd-lib-builder

PDLIBBUILDER_DIR=pd-lib-builder/
include $(firstword $(wildcard $(PDLIBBUILDER_DIR)/Makefile.pdlibbuilder Makefile.pdlibbuilder))
