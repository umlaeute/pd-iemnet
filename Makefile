# To use this Makefile for your project, first put the name of your library in
# LIBRARY_NAME variable. The folder for your project should have the same name
# as your library.
LIBRARY_NAME = iemnet
LIBRARY_VERSION = 0.1

# Next, add your .c source files to the SOURCES variable.  The help files will
# be included automatically
SOURCES = tcpserver.c tcpclient.c tcpsend.c tcpreceive.c udpreceive.c udpsend.c udpclient.c udpserver.c
#SOURCES = tcpclient.c  tcpreceive.c  tcpsend.c  tcpserver.c  udpreceive~.c  udpreceive.c  udpsend~.c  udpsend.c

# For objects that only build on certain platforms, add those to the SOURCES
# line for the right platforms.
SOURCES_android = 
SOURCES_cygwin = 
SOURCES_macosx = 
SOURCES_iphoneos = 
SOURCES_linux = 
SOURCES_windows = 

# .c source files that will be statically linked to _all_ objects
HELPERSOURCES = iemnet.c iemnet_data.c  iemnet_receiver.c  iemnet_sender.c


# list all pd objects (i.e. myobject.pd) files here, and their helpfiles will
# be included automatically
PDOBJECTS = 

# if you want to include any other files in the source and binary tarballs,
# list them here.  This can be anything from header files, READMEs, example
# patches, documentation, etc.
EXTRA_DIST = 

# to enable debugging set this to "-DDEBUG"
# you can slo just run make as "make DEBUG_CFLAGS='-DDEBUG'"
DEBUG_CFLAGS =


#------------------------------------------------------------------------------#
#
# you shouldn't need to edit anything below here, if we did it right :)
#
#------------------------------------------------------------------------------#

# where Pd lives
PD_PATH = ../../../pd
# where to install the library
prefix = /usr/local
libdir = $(prefix)/lib
pkglibdir = $(libdir)/pd-externals
objectsdir = $(pkglibdir)


INSTALL = install
INSTALL_FILE    = $(INSTALL) -p -m 644
INSTALL_LIB     = $(INSTALL) -p -m 644 -s
INSTALL_DIR     = $(INSTALL) -p -m 755 -d

CFLAGS = -DPD -DLIBRARY_VERSION=\"$(LIBRARY_VERSION)\" -I$(PD_PATH)/src -Wall -W -Wno-unused -g 
LDFLAGS =  
LIBS = 
ALLSOURCES := $(SOURCES) $(SOURCES_android) $(SOURCES_cygwin) $(SOURCES_macosx) \
	         $(SOURCES_iphoneos) $(SOURCES_linux) $(SOURCES_windows)

UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
  CPU := $(shell uname -p)
  ifeq ($(CPU),arm) # iPhone/iPod Touch
    SOURCES += $(SOURCES_macosx)
    EXTENSION = pd_darwin
    OS = iphoneos
    IPHONE_BASE=/Developer/Platforms/iPhoneOS.platform/Developer/usr/bin
    CC=$(IPHONE_BASE)/gcc
    CPP=$(IPHONE_BASE)/cpp
    CXX=$(IPHONE_BASE)/g++
    ISYSROOT = -isysroot /Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS3.0.sdk
    IPHONE_CFLAGS = -miphoneos-version-min=3.0 $(ISYSROOT) -arch armv6
    OPT_CFLAGS = -fast -funroll-loops -fomit-frame-pointer
	CFLAGS := $(IPHONE_CFLAGS) $(OPT_CFLAGS) $(CFLAGS) \
      -I/Applications/Pd-extended.app/Contents/Resources/include
    LDFLAGS += -arch armv6 -bundle -undefined dynamic_lookup $(ISYSROOT)
    LIBS += -lc 
    STRIP = strip -x
    DISTDIR=$(LIBRARY_NAME)-$(LIBRARY_VERSION)
    DISTBINDIR=$(DISTDIR)-$(OS)
  else # Mac OS X
    SOURCES += $(SOURCES_macosx)
    EXTENSION = pd_darwin
    OS = macosx
    OPT_CFLAGS = -ftree-vectorize -ftree-vectorizer-verbose=0 -fast
    FAT_FLAGS = -arch i386 -arch ppc -mmacosx-version-min=10.4
    CFLAGS += $(FAT_FLAGS) -fPIC -I/sw/include \
      -I/Applications/Pd-extended.app/Contents/Resources/include
    LDFLAGS += $(FAT_FLAGS) -bundle -undefined dynamic_lookup -L/sw/lib
    LIBS += -lc 
    STRIP = strip -x
    DISTDIR=$(LIBRARY_NAME)-$(LIBRARY_VERSION)
    DISTBINDIR=$(DISTDIR)-$(OS)
  endif
endif
ifeq ($(UNAME),Linux)
  SOURCES += $(SOURCES_linux)
  EXTENSION = pd_linux
  OS = linux
  OPT_CFLAGS = -O6 -funroll-loops -fomit-frame-pointer
  CFLAGS += -fPIC
  LDFLAGS += -Wl,--export-dynamic  -shared -fPIC
  LIBS += -lc
  STRIP = strip --strip-unneeded -R .note -R .comment
  DISTDIR=$(LIBRARY_NAME)-$(LIBRARY_VERSION)
  DISTBINDIR=$(DISTDIR)-$(OS)-$(shell uname -m)
endif
ifeq (CYGWIN,$(findstring CYGWIN,$(UNAME)))
  SOURCES += $(SOURCES_cygwin)
  EXTENSION = dll
  OS = cygwin
  OPT_CFLAGS = -O6 -funroll-loops -fomit-frame-pointer
  CFLAGS += 
  LDFLAGS += -Wl,--export-dynamic -shared -L$(PD_PATH)/src
  LIBS += -lc -lpd
  STRIP = strip --strip-unneeded -R .note -R .comment
  DISTDIR=$(LIBRARY_NAME)-$(LIBRARY_VERSION)
  DISTBINDIR=$(DISTDIR)-$(OS)
endif
ifeq (MINGW,$(findstring MINGW,$(UNAME)))
  SOURCES += $(SOURCES_windows)
  EXTENSION = dll
  OS = windows
  OPT_CFLAGS = -O3 -funroll-loops -fomit-frame-pointer
  WINDOWS_HACKS = -D'O_NONBLOCK=1'
  CFLAGS += -mms-bitfields $(WINDOWS_HACKS)
  LDFLAGS += -s -shared -Wl,--enable-auto-import
  LIBS += -L$(PD_PATH)/src -L$(PD_PATH)/bin -L$(PD_PATH)/obj -lpd -lwsock32 -lkernel32 -luser32 -lgdi32
  STRIP = strip --strip-unneeded -R .note -R .comment
  DISTDIR=$(LIBRARY_NAME)-$(LIBRARY_VERSION)
  DISTBINDIR=$(DISTDIR)-$(OS)
endif

CFLAGS += $(OPT_CFLAGS)
CFLAGS += $(DEBUG_CFLAGS)


.PHONY = install libdir_install single_install install-doc install-exec clean dist etags

all: $(SOURCES:.c=.$(EXTENSION))

%.o: %.c
	$(CC) $(CFLAGS) -o "$@" -c "$<"

%.$(EXTENSION): %.o $(HELPERSOURCES:.c=.o)
	$(CC) $(LDFLAGS) -o "$@" $^  $(LIBS)
	chmod a-x "$*.$(EXTENSION)"

# this links everything into a single binary file
$(LIBRARY_NAME): $(SOURCES:.c=.o) $(LIBRARY_NAME).o $(HELPERSOURCES:.c=.o)
	$(CC) $(LDFLAGS) -o $@.$(EXTENSION) $^ $(LIBS)
	chmod a-x $@.$(EXTENSION)


install: libdir_install

# The meta and help files are explicitly installed to make sure they are
# actually there.  Those files are not optional, then need to be there.
libdir_install: $(SOURCES:.c=.$(EXTENSION)) install-doc
	$(INSTALL_DIR) $(DESTDIR)$(objectsdir)/$(LIBRARY_NAME)
	$(INSTALL_FILE) $(LIBRARY_NAME)-meta.pd \
		$(DESTDIR)$(objectsdir)/$(LIBRARY_NAME)
	test -z "$(SOURCES)" || \
		$(INSTALL_LIB) $(SOURCES:.c=.$(EXTENSION)) \
			$(DESTDIR)$(objectsdir)/$(LIBRARY_NAME)
	test -z "$(PDOBJECTS)" || \
		$(INSTALL_LIB) $(OBJECTS) \
			$(DESTDIR)$(objectsdir)/$(LIBRARY_NAME)

# install library linked as single binary
single_install: $(LIBRARY_NAME) install-doc install-exec
	$(INSTALL_DIR) $(DESTDIR)$(objectsdir)/$(LIBRARY_NAME)
	$(INSTALL_FILE) $(LIBRARY_NAME).$(EXTENSION) $(DESTDIR)$(objectsdir)/$(LIBRARY_NAME)
	$(STRIP) $(DESTDIR)$(objectsdir)/$(LIBRARY_NAME)/$(LIBRARY_NAME).$(EXTENSION)

install-doc:
	$(INSTALL_DIR) $(DESTDIR)$(objectsdir)/$(LIBRARY_NAME)
	test -z "$(SOURCES)" || \
		$(INSTALL_FILE) $(SOURCES:.c=-help.pd) \
			$(DESTDIR)$(objectsdir)/$(LIBRARY_NAME)
	test -z "$(PDOBJECTS)" || \
		$(INSTALL_FILE) $(PDOBJECTS:.pd=-help.pd) \
			$(DESTDIR)$(objectsdir)/$(LIBRARY_NAME)
	$(INSTALL_FILE) README.txt $(DESTDIR)$(objectsdir)/$(LIBRARY_NAME)/README.txt
	$(INSTALL_FILE) VERSION.txt $(DESTDIR)$(objectsdir)/$(LIBRARY_NAME)/VERSION.txt
	$(INSTALL_FILE) CHANGES.txt $(DESTDIR)$(objectsdir)/$(LIBRARY_NAME)/CHANGES.txt


clean:
	-rm -f -- $(SOURCES:.c=.o) $(HELPERSOURCES:.c=.o)
	-rm -f -- $(SOURCES:.c=.$(EXTENSION))
	-rm -f -- $(LIBRARY_NAME).$(EXTENSION)

distclean: clean
	-rm -f *~ *.o *.$(EXTENSION)
	-rm -f -- $(DISTBINDIR).tar.gz
	-rm -rf -- $(DISTBINDIR)
	-rm -f -- $(DISTDIR).tar.gz
	-rm -rf -- $(DISTDIR)


$(DISTBINDIR):
	$(INSTALL_DIR) $(DISTBINDIR)

libdir: all $(DISTBINDIR)
	$(INSTALL_FILE) $(LIBRARY_NAME)-meta.pd  $(DISTBINDIR)
	$(INSTALL_FILE) $(SOURCES)  $(DISTBINDIR)
	$(INSTALL_FILE) $(SOURCES:.c=-help.pd) $(DISTBINDIR)
	test -z "$(EXTRA_DIST)" || \
		$(INSTALL_FILE) $(EXTRA_DIST)    $(DISTBINDIR)
#	tar --exclude-vcs -czpf $(DISTBINDIR).tar.gz $(DISTBINDIR)

$(DISTDIR):
	$(INSTALL_DIR) $(DISTDIR)

dist: $(DISTDIR)
	$(INSTALL_FILE) Makefile  $(DISTDIR)
	$(INSTALL_FILE) $(LIBRARY_NAME)-meta.pd  $(DISTDIR)
	test -z "$(ALLSOURCES)" || \
		$(INSTALL_FILE) $(ALLSOURCES)  $(DISTDIR)
	test -z "$(ALLSOURCES)" || \
		$(INSTALL_FILE) $(ALLSOURCES:.c=-help.pd) $(DISTDIR)
	test -z "$(PDOBJECTS)" || \
		$(INSTALL_FILE) $(PDOBJECTS)  $(DISTDIR)
	test -z "$(PDOBJECTS)" || \
		$(INSTALL_FILE) $(PDOBJECTS:.pd=-help.pd) $(DISTDIR)
	test -z "$(EXTRA_DIST)" || \
		$(INSTALL_FILE) $(EXTRA_DIST)    $(DISTDIR)
	tar --exclude-vcs -czpf $(DISTDIR).tar.gz $(DISTDIR)


etags:
	etags *.h $(SOURCES) ../../pd/src/*.[ch] /usr/include/*.h /usr/include/*/*.h

showpaths:
	@echo "PD_PATH: $(PD_PATH)"
	@echo "objectsdir: $(objectsdir)"
	@echo "LIBRARY_NAME: $(LIBRARY_NAME)"
	@echo "SOURCES: $(SOURCES)"
	@echo "ALLSOURCES: $(ALLSOURCES)"
	@echo "UNAME: $(UNAME)"
	@echo "CPU: $(CPU)"

