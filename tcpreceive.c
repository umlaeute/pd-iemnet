/* tcpreceive.c
 * copyright © 2010-2015 IOhannes m zmölnig, IEM
 * copyright (c) 2006-2010 Martin Peach
 * copyright (c) Miller Puckette
 */

/*                                                                              */
/* A client for unidirectional communication from within Pd.                     */
/*                                                                              */
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

#define DEBUGLEVEL 1

static const char*objName = "tcpreceive";

#include "iemnet.h"
#ifndef _WIN32
/* needed for TCP_NODELAY */
# include <netinet/tcp.h>
#endif

#include <string.h>

/* ----------------------------- tcpreceive ------------------------- */

static t_class *tcpreceive_class;

#define MAX_CONNECTIONS 128 /* this is going to cause trouble down the line...:( */

typedef struct _tcpconnection {
  struct sockaddr_storage address;
  int socket;
  struct _tcpreceive*owner;
  t_iemnet_receiver*receiver;
} t_tcpconnection;

typedef struct _tcpreceive {
  t_object x_obj;
  t_outlet*x_msgout;
  t_outlet*x_addrout;
  t_outlet*x_connectout;
  t_outlet*x_statusout;
  int x_connectsocket[2];
  int x_port;
  t_symbol*x_host;

  int x_serialize;

  int x_nconnections;
  t_tcpconnection x_connection[MAX_CONNECTIONS];

  t_iemnet_floatlist*x_floatlist;
} t_tcpreceive;

/* forward declarations */
static int tcpreceive_disconnect(t_tcpreceive *x, int id);

static int tcpreceive_find_socket(t_tcpreceive *x, int fd)
{
  int i;
  for (i = 0; i < MAX_CONNECTIONS; ++i)
    if (x->x_connection[i].socket == fd) {
      return i;
    }

  return -1;
}

static void tcpreceive_read_callback(void *w, t_iemnet_chunk*c)
{
  t_tcpconnection*y = (t_tcpconnection*)w;
  t_tcpreceive*x = NULL;
  int index = -1;
  if(NULL == y || NULL == (x = y->owner)) {
    return;
  }

  index = tcpreceive_find_socket(x, y->socket);
  if(index >= 0) {
    if(c) {
      /* TODO?: outlet info about connection */

      /* gets destroyed in the dtor */
      x->x_floatlist = iemnet__chunk2list(c, x->x_floatlist);
      iemnet__streamout(x->x_msgout, x->x_floatlist->argc, x->x_floatlist->argv,
                        x->x_serialize);
    } else {
      /* disconnected */
      tcpreceive_disconnect(x, index);
    }
  }
}

/* tcpreceive_addconnection tries to add the socket fd to the list */
/* returns 1 on success, else 0 */
static int tcpreceive_addconnection(t_tcpreceive *x, int fd, const struct sockaddr_storage*address)
{
  int i;
  for (i = 0; i < MAX_CONNECTIONS; ++i) {
    if (x->x_connection[i].socket == -1) {
      x->x_connection[i].socket = fd;
      memcpy(&x->x_connection[i].address, address, sizeof(*address));
      x->x_connection[i].owner = x;
      x->x_connection[i].receiver =
        iemnet__receiver_create(fd,
                                x->x_connection+i,
                                tcpreceive_read_callback,
                                0);
      return 1;
    }
  }
  return 0;
}


/* tcpreceive_connectpoll checks for incoming connection requests on the original socket */
/* a new socket is assigned  */
static void tcpreceive_connectpoll(t_tcpreceive *x, int fd)
{
  struct sockaddr_storage from;
  socklen_t fromlen = sizeof(from);
  if(fd != x->x_connectsocket[0] && fd != x->x_connectsocket[1]) {
    iemnet_log(x, IEMNET_FATAL, "callback received for socket:%d on listener for socket:%d/%d", fd, x->x_connectsocket[0], x->x_connectsocket[1]);
    return;
  }

  fd = accept(fd, (struct sockaddr *)&from, &fromlen);
  if (fd < 0) {
    iemnet_log(x, IEMNET_ERROR, "could not accept new connection");
    sys_sockerror("accept");
  } else {
    /* get the sender's ip */
    if (tcpreceive_addconnection(x, fd, &from)) {
      x->x_nconnections++;
      iemnet__numconnout(x->x_statusout, x->x_connectout, x->x_nconnections);
      iemnet__addrout(x->x_statusout, x->x_addrout, &from);
    } else {
      iemnet_log(x, IEMNET_ERROR, "too many connections");
      iemnet__closesocket(fd, 1);
    }
  }
}

static int tcpreceive_disconnect(t_tcpreceive *x, int id)
{
  if(id >= 0 && id < MAX_CONNECTIONS && x->x_connection[id].address.ss_family>0) {
    iemnet__receiver_destroy(x->x_connection[id].receiver, 0);
    x->x_connection[id].receiver = NULL;

    iemnet__closesocket(x->x_connection[id].socket, 1);
    x->x_connection[id].socket = -1;
    memset(&x->x_connection[id].address, 0, sizeof(x->x_connection[id].address));
    x->x_nconnections--;
    iemnet__numconnout(x->x_statusout, x->x_connectout, x->x_nconnections);
    return 1;
  }

  return 0;
}


/* tcpreceive_closeall closes all open sockets and deletes them from the list */
static void tcpreceive_disconnect_all(t_tcpreceive *x)
{
  int i;

  for (i = 0; i < MAX_CONNECTIONS; i++) {
    tcpreceive_disconnect(x, i);
  }
}

#if 0
/* tcpreceive_removeconnection tries to delete the socket fd from the list */
/* returns 1 on success, else 0 */
static int tcpreceive_disconnect_socket(t_tcpreceive *x, int fd)
{
  int i;
  for (i = 0; i < MAX_CONNECTIONS; ++i) {
    if (x->x_connection[i].socket == fd) {
      return tcpreceive_disconnect(x, i);
    }
  }
  return 0;
}
#endif
static void tcpreceive_do_listen(t_tcpreceive*x, const char*hostname, int portno)
{
  static t_atom ap[2];
  struct sockaddr_in server;
  socklen_t serversize = sizeof(server);
  int sockfd[2];
  struct addrinfo *ailist = NULL, *ai;
  int err;
  int i;

  sockfd[0] = x->x_connectsocket[0];
  sockfd[1] = x->x_connectsocket[1];

  SETFLOAT(ap, -1);
  memset(&server, 0, sizeof(server));


  if((x->x_port == portno) && ((!hostname && !x->x_host) || (hostname && gensym(hostname) == x->x_host))) {
    return;
  }

  /* cleanup any open ports */
  for(i=0; i<2; i++) {
    sockfd[i] = x->x_connectsocket[i];
    if(sockfd[i] >= 0) {
      sys_rmpollfn(sockfd[i]);
      iemnet__closesocket(sockfd[i], 1);
      x->x_connectsocket[i] = -1;
    }
  }
  x->x_port = -1;
  x->x_host = NULL;

  if ((err=iemnet__getaddrinfo(&ailist, hostname, portno, 0, SOCK_STREAM))) {
    iemnet_log(x, IEMNET_ERROR, "%s (%d)\n\tbad host ('%s') or port (%d)?",
               gai_strerror(err), err,
               hostname?hostname:"*", portno);
    return;
  }
  if (hostname) {
#warning FIXXME: IPv6 support lacks IPv4 fallbacks
    /* TODO: if there are both IPv4 and IPv6 addresses, bind to both */
  }

  for (ai = ailist; ai != NULL; ai = ai->ai_next) {
    char buf[MAXPDSTRING];
    int fd = -1;
    /* create a socket */
    if (sockfd[AF_INET6 == ai->ai_family] >= 0)
      continue;
    iemnet__post_addrinfo(ai);
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0)
      continue;

    /* ask OS to allow another Pd to reopen this port after we close it. */
#ifdef SO_REUSEADDR
    if (iemnet__setsockopti(fd, SOL_SOCKET, SO_REUSEADDR, 1) < 0) {
      iemnet_log(x, IEMNET_ERROR, "unable to enable address re-using");
      sys_sockerror("setsockopt:SO_REUSEADDR");
    }
#endif /* SO_REUSEADDR */
#ifdef SO_REUSEPORT
    if (iemnet__setsockopti(fd, SOL_SOCKET, SO_REUSEPORT, 1) < 0) {
      iemnet_log(x, IEMNET_ERROR, "unable to enable port re-using");
      sys_sockerror("setsockopt:SO_REUSEPORT");
    }
#endif /* SO_REUSEPORT */

    /* Stream (TCP) sockets are set NODELAY */
    if (iemnet__setsockopti(fd, IPPROTO_TCP, TCP_NODELAY, 1) < 0) {
      iemnet_log(x, IEMNET_ERROR, "unable to enable immediate sending");
      sys_sockerror("setsockopt:TCP_NODELAY");
    }

    /* if this is the IPv6 "any" address, also listen to IPv4 adapters
       (if not supported, fall back to IPv4) */
    if (!hostname && ai->ai_family == AF_INET6 &&
        iemnet__setsockopti(fd, IPPROTO_IPV6, IPV6_V6ONLY, 0) < 0)
    {
      /* post("netreceive: setsockopt (IPV6_V6ONLY) failed"); */
      sys_closesocket(fd);
      continue;
    }

    /* name the socket */
    if (bind(fd, ai->ai_addr, ai->ai_addrlen) < 0)
    {
      sys_closesocket(fd);
      continue;
    }
    if (hostname) {
      iemnet_log(x, IEMNET_VERBOSE, "listening on %s:%d",
                 iemnet__sockaddr2str((struct sockaddr_storage*)ai->ai_addr, buf, MAXPDSTRING),
                 portno);
    } else {
      iemnet_log(x, IEMNET_VERBOSE, "listening on %d", portno);
    }
    sockfd[AF_INET6 == ai->ai_family] = fd;

    if (sockfd[0]>=0 && sockfd[1]>=0)
      break;
  }
  freeaddrinfo(ailist);


  if(sockfd[0]<0 && sockfd[1]<0) {
    SETFLOAT(ap, -1);
    iemnet_log(x, IEMNET_ERROR, "unable to create socket for %s:%d", hostname?hostname:"*", portno);
    sys_sockerror("socket");
    outlet_anything(x->x_statusout, gensym("port"), 1, ap);
    return;
  }


  /* streaming protocol */
  err=1;
  for (i=0; i<2; i++) {
    if(sockfd[i]<0)
      continue;
    if (listen(sockfd[i], 5) < 0) {
      SETFLOAT(ap, -1);
      iemnet_log(x, IEMNET_ERROR, "unable to listen on socket");
      sys_sockerror("listen");
      iemnet__closesocket(sockfd[i], 1);
      sockfd[i] = -1;
    } else
      err=0;
  }
  if(err) {
    SETFLOAT(ap, -1);
    outlet_anything(x->x_statusout, gensym("port"), 1, ap);
    return;
  }

  x->x_host = hostname?gensym(hostname):0;
  x->x_port = portno;

  /* wait for new connections */
  for (i=0; i<2; i++) {
    SETFLOAT(ap+i, 0);
    if(sockfd[i]<0)
      continue;

    sys_addpollfn(sockfd[i],
                  (t_fdpollfn)tcpreceive_connectpoll,
                  x);
    x->x_connectsocket[i] = sockfd[i];
    /* find out which port is actually used (useful when assigning "0") */
    if(!getsockname(sockfd[i], (struct sockaddr *)&server, &serversize)) {
      x->x_port = ntohs(server.sin_port);
      SETFLOAT(ap+i, x->x_port);
    }
  }

  outlet_anything(x->x_statusout, gensym("port"), 2, ap);
}
static void tcpreceive_listen(t_tcpreceive*x, t_symbol*s, int argc, t_atom*argv) {
  const char*host=0;
  t_symbol*shost=0;
  int port = x->x_port;
  switch(argc) {
  default:
    pd_error(x, "invalid arguments: use '%s <hostname> [<port>]'", s->s_name);
    return;
  case 2:
    port = atom_getfloat(argv+1);
    /* fallthrough */
  case 1:
    shost = atom_getsymbol(argv+0);
    break;
  }
  if(shost && gensym("") != shost)
    host = shost->s_name;
  tcpreceive_do_listen(x, host, port);
}
static void tcpreceive_port(t_tcpreceive*x, t_floatarg fportno)
{
  int portno = fportno;
  const char*host=NULL;
  if(x->x_host && gensym("") != x->x_host)
    host = x->x_host->s_name;
  tcpreceive_do_listen(x, host, portno);
}

static void tcpreceive_serialize(t_tcpreceive *x, t_floatarg doit)
{
  x->x_serialize = doit;
}

static void tcpreceive_free(t_tcpreceive *x)
{
  /* is this ever called? */
  int i;
  for(i=0; i<2; i++) {
    if (x->x_connectsocket[i] >= 0) {
      sys_rmpollfn(x->x_connectsocket[i]);
      iemnet__closesocket(x->x_connectsocket[i], 1);
    }
  }
  tcpreceive_disconnect_all(x);
  if(x->x_floatlist) {
    iemnet__floatlist_destroy(x->x_floatlist);
  }
  x->x_floatlist = NULL;
}

static void *tcpreceive_new(t_floatarg fportno)
{
  t_tcpreceive*x;
  int portno = fportno;
  int i;

  x = (t_tcpreceive *)pd_new(tcpreceive_class);
  x->x_msgout = outlet_new(&x->x_obj, 0);
  x->x_addrout = outlet_new(&x->x_obj, gensym("list")); /* legacy */
  x->x_connectout = outlet_new(&x->x_obj, gensym("float")); /* legacy */
  x->x_statusout = outlet_new(&x->x_obj, 0); /* outlet for everything else */

  x->x_serialize = 1;

  x->x_connectsocket[0] = -1;
  x->x_connectsocket[1] = -1;
  x->x_port = -1;
  x->x_host = 0;
  x->x_nconnections = 0;

  /* clear the connection list */
  for (i = 0; i < MAX_CONNECTIONS; ++i) {
    x->x_connection[i].socket = -1;
    memset(&x->x_connection[i].address, 0, sizeof(x->x_connection[i].address));
  }

  x->x_floatlist = iemnet__floatlist_create(1024);

  tcpreceive_port(x, portno);

  return (x);
}


IEMNET_EXTERN void tcpreceive_setup(void)
{
  if(!iemnet__register(objName)) {
    return;
  }
  tcpreceive_class = class_new(gensym(objName),
                               (t_newmethod)tcpreceive_new, (t_method)tcpreceive_free,
                               sizeof(t_tcpreceive),
                               0,
                               A_DEFFLOAT, 0);

  class_addmethod(tcpreceive_class, (t_method)tcpreceive_port,
                  gensym("port"), A_DEFFLOAT, 0);
  class_addmethod(tcpreceive_class, (t_method)tcpreceive_listen,
                  gensym("listen"), A_GIMME, 0);

  class_addmethod(tcpreceive_class, (t_method)tcpreceive_serialize,
                  gensym("serialize"), A_FLOAT, 0);
  DEBUGMETHOD(tcpreceive_class);
}

IEMNET_INITIALIZER(tcpreceive_setup);

/* end x_net_tcpreceive.c */
