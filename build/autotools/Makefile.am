AUTOMAKE_OPTIONS = foreign
AM_CPPFLAGS = -I$(top_srcdir)

lib_LTLIBRARIES = libiemnet.la

libiemnet_la_LDFLAGS = -version-info @SHARED_VERSION_INFO@ -no-undefined
libiemnet_la_CFLAGS  = 
#libiemnet_la_CFLAGS += -fvisibility=hidden
libiemnet_la_LIBADD  = $(LIBM)

libiemnet_la_SOURCES = \
	../../iemnet.c \
	../../iemnet_data.c \
	../../iemnet_receiver.c \
	../../iemnet_sender.c \
	../../iemnet_data.h \
	../../iemnet.h

