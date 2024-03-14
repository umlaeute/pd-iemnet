/* iemnet
 * this file provides core infrastructure for the iemnet-objects
 *
 *  copyright © 2010-2020 IOhannes m zmölnig, IEM
 */

/* This program is free software; you can redistribute it and/or                */
/* modify it under the terms of the GNU General Public License                  */
/* as published by the Free Software Foundation; either version 2               */
/* of the License, or (at your option) any later version.                       */
/*                                                                              */
/* This program is distributed in the hope that it will be useful,              */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of               */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                */
/* GNU General Public License for more details.                                 */
/*                                                                              */
/* You should have received a copy of the GNU General Public License            */
/* along with this program; if not, see                                         */
/*     http://www.gnu.org/licenses/                                             */
/*                                                                              */

#define DEBUGLEVEL

#include "iemnet.h"
#include <stdlib.h>

#include <pthread.h>

#ifdef __unix__
# include <sys/un.h>
#endif

#ifndef PD_VERSION_CODE
# define PD_VERSION_CODE IEMNET_VERSION(PD_MAJOR_VERSION, PD_MINOR_VERSION, PD_BUGFIX_VERSION)
#endif


#if PD_VERSION_CODE < IEMNET_VERSION(0, 52, 0)
# define PD_DEBUG 3
#endif

/* close a socket properly */
void iemnet__closesocket(int sockfd, int verbose)
{
  if(sockfd >= 0) {
#ifndef SHUT_RDWR
# define SHUT_RDWR 2
#endif
    int how = SHUT_RDWR;
    /* needed on linux, since the recv won't shutdown on sys_closesocket() alone */
    int err = shutdown(sockfd, how);
    if(verbose && err) {
      perror("iemnet:socket-shutdown");
    }
    sys_closesocket(sockfd);
  }
}

int iemnet__sockaddr2list(const struct sockaddr_storage*address, t_atom alist[18])
{
  switch (address->ss_family) {
  case AF_INET: {
    struct sockaddr_in*addr = (struct sockaddr_in*)address;
    uint32_t ipaddr = ntohl(addr->sin_addr.s_addr);
    SETSYMBOL(alist+0, gensym("IPv4"));
    SETFLOAT(alist+1, (ipaddr & 0xFF000000)>>24);
    SETFLOAT(alist+2, (ipaddr & 0x0FF0000)>>16);
    SETFLOAT(alist+3, (ipaddr & 0x0FF00)>>8);
    SETFLOAT(alist+4, (ipaddr & 0x0FF));
    SETFLOAT(alist+5, ntohs(addr->sin_port));
    return 6;
  }
    break;
  case AF_INET6: {
    struct sockaddr_in6*addr = (struct sockaddr_in6*)address;
    uint8_t*ipaddr = addr->sin6_addr.s6_addr;
    unsigned int i;
    SETSYMBOL(alist+0, gensym("IPv6"));
    for(i = 0; i<16; i++) {
      SETFLOAT(alist+i+1, ipaddr[i]);
    }
    SETFLOAT(alist+17, ntohs(addr->sin6_port));
    return 18;
  }
    break;
#ifdef __unix__
  case AF_UNIX: {
    struct sockaddr_un*addr = (struct sockaddr_un*)&address;
    SETSYMBOL(alist+0, gensym("unix"));
    SETSYMBOL(alist+1, gensym(addr->sun_path));
    return 2;
  }
    break;
#endif
  default:
    break;
  }
  return 0;
}


void iemnet__socket2addressout(int sockfd, t_outlet*status_outlet, t_symbol*s)
{
  struct sockaddr_storage address;
  socklen_t addresssize = sizeof(address);
  t_atom alist[18];
  int alen;
  if (getsockname(sockfd, (struct sockaddr *) &address, &addresssize)) {
    pd_error(0, "unable to get address from socket:%d", sockfd);
    return;
  }
  if ((alen = iemnet__sockaddr2list(&address, alist))) {
    outlet_anything(status_outlet, s, alen, alist);
  } else {
    pd_error(0, "unknown address-family:0x%02X on socket:%d", address.ss_family, sockfd);
  }
}

/* various functions to send data to output in a uniform way */
void iemnet__addrout(t_outlet*status_outlet, t_outlet*address_outlet,
                     uint32_t address, uint16_t port)
{
  static t_atom addr[5];
  static int firsttime = 1;

  if(firsttime) {
    int i = 0;
    for(i = 0; i<5; i++) {
      SETFLOAT(addr+i, 0);
    }
    firsttime = 0;
  }

  addr[0].a_w.w_float = (address & 0xFF000000)>>24;
  addr[1].a_w.w_float = (address & 0x0FF0000)>>16;
  addr[2].a_w.w_float = (address & 0x0FF00)>>8;
  addr[3].a_w.w_float = (address & 0x0FF);
  addr[4].a_w.w_float = port;

  if(status_outlet ) {
    outlet_anything(status_outlet, gensym("address"), 5, addr);
  }
  if(address_outlet) {
    outlet_list(address_outlet, gensym("list"   ), 5, addr);
  }
}

void iemnet__numconnout(t_outlet*status_outlet, t_outlet*numcon_outlet,
                        int numconnections)
{
  t_atom atom[1];
  SETFLOAT(atom, numconnections);

  if(status_outlet) {
    outlet_anything(status_outlet, gensym("connections"), 1, atom);
  }
  if(numcon_outlet) {
    outlet_float(numcon_outlet, numconnections);
  }
}

void iemnet__socketout(t_outlet*status_outlet, t_outlet*socket_outlet,
                       int socketfd)
{
  t_atom atom[1];
  SETFLOAT(atom, socketfd);

  if(status_outlet) {
    outlet_anything(status_outlet, gensym("socket"), 1, atom);
  }
  if(socket_outlet) {
    outlet_float(socket_outlet, socketfd);
  }
}


void iemnet__streamout(t_outlet*outlet, int argc, t_atom*argv, int stream)
{
  if(NULL == outlet) {
    return;
  }

  if(stream) {
    while(argc-->0) {
      outlet_list(outlet, gensym("list"), 1, argv);
      argv++;
    }
  } else {
    outlet_list(outlet, gensym("list"), argc, argv);
  }
}

typedef struct _names {
  t_symbol*name;
  struct _names*next;
} t_iemnet_names;
static t_iemnet_names*namelist = 0;
static int iemnet__nametaken(const char*namestring)
{
  t_symbol*name = gensym(namestring);
  t_iemnet_names*curname = namelist;
  t_iemnet_names*lastname = curname;
  while(curname) {
    if(name == (curname->name)) {
      return 1;
    }
    lastname = curname;
    curname = curname->next;
  }

  /* new name! */
  curname = (t_iemnet_names*)malloc(sizeof(t_iemnet_names));
  curname->name = name;
  curname->next = 0;

  if(lastname) {
    lastname->next = curname;
  } else {
    namelist = curname;
  }

  return 0;
}

#ifndef BUILD_DATE
# define BUILD_DATE "on " __DATE__ " at " __TIME__
#endif

int iemnet__register(const char*name)
{
  static int firsttime = 1;
  if(iemnet__nametaken(name)) {
    return 1;
  }
  if(firsttime) {
    post("iemnet - networking with Pd");
#ifdef VERSION
    post("        version "VERSION"");
#endif
    post("        compiled "BUILD_DATE"");
    post("        copyright © 2010-2015 IOhannes m zmoelnig, IEM");
    post("        based on mrpeach/net, based on maxlib");
  }
#if PD_VERSION_CODE > IEMNET_VERSION(0, 43, 0)
  logpost(0, PD_DEBUG, "iemnet: loaded [%s]", name);
#else
  post("iemnet: loaded [%s]", name);
#endif

  firsttime = 0;
  return 1;
}




#ifdef IEMNET_HAVE_DEBUG
static int iemnet_debuglevel_ = 0;
static pthread_mutex_t debug_mtx = PTHREAD_MUTEX_INITIALIZER;
#endif

void iemnet_debuglevel(void*x, t_float f)
{
  static int firsttime = 1;
#ifdef IEMNET_HAVE_DEBUG
  int debuglevel = (int)f;

  pthread_mutex_lock(&debug_mtx);
  iemnet_debuglevel_ = debuglevel;
  pthread_mutex_unlock(&debug_mtx);

  post("iemnet: setting debuglevel to %d", debuglevel);
#else
  if(firsttime) {
    pd_error(x, "iemnet compiled without debug!");
  }
#endif
  if(firsttime) {
    (void)x; /* ignore unused variable */
    (void)f; /* ignore unused variable */
  }
  firsttime = 0;
}

int iemnet_debug(int debuglevel, const char*file, unsigned int line,
                 const char*function)
{
#ifdef IEMNET_HAVE_DEBUG
  int debuglevel_ = 0;
  pthread_mutex_lock(&debug_mtx);
  debuglevel_ = iemnet_debuglevel_;
  pthread_mutex_unlock(&debug_mtx);
  if(debuglevel_ & debuglevel) {
    startpost("[%s[%d]:%s#%d] ", file, line, function, debuglevel);
    return 1;
  }
#else
  /* silence up 'unused parameter' warnings */
  (void)debuglevel;
  (void)file;
  (void)line;
  (void)function;
#endif
  return 0;
}

#include <stdarg.h>
#include <string.h>
#include <m_imp.h>

void iemnet_log(const void *object, const t_iemnet_loglevel level, const char *fmt, ...)
{
  t_pd*x = (t_pd*)object;
  const char*name = (x && (*x) && ((*x)->c_name))?((*x)->c_name->s_name):"iemnet";
  char buf[MAXPDSTRING];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, MAXPDSTRING-1, fmt, ap);
  va_end(ap);
  strcat(buf, "\0");
#if PD_VERSION_CODE > IEMNET_VERSION(0, 43, 0)
  logpost(x, level, "[%s]: %s", name, buf);
#else
  if(level>1) {
    post("[%s]: %s", name, buf);
  } else {
    pd_error(x, "[%s]: %s", name, buf);
  }
#endif
}
