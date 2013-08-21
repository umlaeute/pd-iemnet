AUTOMAKE_OPTIONS = foreign
AM_LDFLAGS= -module -avoid-version -shared -shrext .pd_linux

TEST_EXTENSIONS = .la
LA_LOG_COMPILER = $(srcdir)/runtest.sh

AM_CPPFLAGS=-I$(top_srcdir)/../..
AM_LDFLAGS+=$(top_builddir)/libiemnet.la -rpath /tmp

SOURCES=common.h
noinst_HEADERS=common.h 
EXTRA_DIST=runtest.sh

TESTS = \
        pass.la skip.la fail.la \
	serialqueue.la threadedqueue.la

XFAIL_TESTS = fail.la

check_LTLIBRARIES= \
        pass.la skip.la fail.la \
	serialqueue.la threadedqueue.la

pass_la_SOURCES=pass.c
skip_la_SOURCES=skip.c
fail_la_SOURCES=fail.c

threadedqueue_la_SOURCES=threadedqueue.c
serialqueue_la_SOURCES=serialqueue.c

