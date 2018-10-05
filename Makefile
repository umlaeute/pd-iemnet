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
        udpserver.c

shared.sources = \
	iemnet.c \
	iemnet_data.c \
	iemnet_receiver.c \
	iemnet_sender.c

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
	LICENSE.txt \
	README.txt

define forWindows
  ldlibs = -lwsock32 -lpthread
endef

# This Makefile is based on the Makefile from pd-lib-builder written by
# Katja Vetter. You can get it from:
# https://github.com/pure-data/pd-lib-builder

PDLIBBUILDER_DIR=pd-lib-builder/
include $(firstword $(wildcard $(PDLIBBUILDER_DIR)/Makefile.pdlibbuilder Makefile.pdlibbuilder))
