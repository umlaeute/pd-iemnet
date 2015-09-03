AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4

SUBDIRS=tests

AM_CPPFLAGS = -I$(top_srcdir)

lib_LTLIBRARIES = libiemnet.la

libiemnet_la_LDFLAGS = -version-info 1 -no-undefined
libiemnet_la_CFLAGS  = 
#libiemnet_la_CFLAGS += -fvisibility=hidden
libiemnet_la_LIBADD  = $(LIBM)

libiemnet_la_SOURCES = \
	$(top_srcdir)/../../iemnet_data.c \
	$(top_srcdir)/../../iemnet_data.h \
	$(top_srcdir)/../../iemnet_receiver.c \
	$(top_srcdir)/../../iemnet_sender.c \
	$(top_srcdir)/../../iemnet.c \
	$(top_srcdir)/../../iemnet.h
