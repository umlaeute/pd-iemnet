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
  long addr;
  unsigned short port;
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
  int x_connectsocket;
  int x_port;

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
static int tcpreceive_addconnection(t_tcpreceive *x, int fd, long addr,
                                    unsigned short port)
{
  int i;
  for (i = 0; i < MAX_CONNECTIONS; ++i) {
    if (x->x_connection[i].socket == -1) {
      x->x_connection[i].socket = fd;
      x->x_connection[i].addr = addr;
      x->x_connection[i].port = port;
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
  struct sockaddr_in from;
  socklen_t fromlen = sizeof(from);
  long addr;
  unsigned short port;
  if(fd != x->x_connectsocket) {
    iemnet_log(x, IEMNET_FATAL, "callback received for socket:%d on listener for socket:%d", fd, x->x_connectsocket);
    return;
  }

  fd = accept(fd, (struct sockaddr *)&from, &fromlen);
  if (fd < 0) {
    iemnet_log(x, IEMNET_ERROR, "could not accept new connection");
    sys_sockerror("accept");
  } else {
    /* get the sender's ip */
    addr = ntohl(from.sin_addr.s_addr);
    port = ntohs(from.sin_port);
    if (tcpreceive_addconnection(x, fd, addr, port)) {
      x->x_nconnections++;
      iemnet__numconnout(x->x_statusout, x->x_connectout, x->x_nconnections);
      iemnet__addrout(x->x_statusout, x->x_addrout, addr, port);
    } else {
      iemnet_log(x, IEMNET_ERROR, "too many connections");
      iemnet__closesocket(fd, 1);
    }
  }
}

static int tcpreceive_disconnect(t_tcpreceive *x, int id)
{
  if(id >= 0 && id < MAX_CONNECTIONS && x->x_connection[id].port>0) {
    iemnet__receiver_destroy(x->x_connection[id].receiver, 0);
    x->x_connection[id].receiver = NULL;

    iemnet__closesocket(x->x_connection[id].socket, 1);
    x->x_connection[id].socket = -1;

    x->x_connection[id].addr = 0L;
    x->x_connection[id].port = 0;
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

static void tcpreceive_port(t_tcpreceive*x, t_floatarg fportno)
{
  static t_atom ap[1];
  int portno = fportno;
  struct sockaddr_in server;
  socklen_t serversize = sizeof(server);
  int sockfd = x->x_connectsocket;
  int intarg;
  memset(&server, 0, sizeof(server));

  SETFLOAT(ap, -1);
  if(x->x_port == portno) {
    return;
  }

  /* cleanup any open ports */
  if(sockfd >= 0) {
    sys_rmpollfn(sockfd);
    iemnet__closesocket(sockfd, 1);
    x->x_connectsocket = -1;
    x->x_port = -1;
  }

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if(sockfd<0) {
    iemnet_log(x, IEMNET_ERROR, "unable to create socket");
    sys_sockerror("socket");
    return;
  }

  /* ask OS to allow another Pd to reopen this port after we close it. */
#ifdef SO_REUSEADDR
  intarg = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
                 (char *)&intarg, sizeof(intarg))
      < 0) {
    iemnet_log(x, IEMNET_ERROR, "unable to enable address re-using");
    sys_sockerror("setsockopt:SO_REUSEADDR");
  }
#endif /* SO_REUSEADDR */
#ifdef SO_REUSEPORT
  intarg = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT,
                 (char *)&intarg, sizeof(intarg))
      < 0) {
    iemnet_log(x, IEMNET_ERROR, "unable to enable port re-using");
    sys_sockerror("setsockopt:SO_REUSEPORT");
  }
#endif /* SO_REUSEPORT */

  /* Stream (TCP) sockets are set NODELAY */
  intarg = 1;
  if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY,
                 (char *)&intarg, sizeof(intarg)) < 0) {
    iemnet_log(x, IEMNET_ERROR, "unable to enable immediate sending");
    sys_sockerror("setsockopt:TCP_NODELAY");
  }

  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons((u_short)portno);

  /* name the socket */
  if (bind(sockfd, (struct sockaddr *)&server, serversize) < 0) {
    iemnet_log(x, IEMNET_ERROR, "couldn't bind socket");
    sys_sockerror("bind");
    iemnet__closesocket(sockfd, 1);
    sockfd = -1;
    outlet_anything(x->x_statusout, gensym("port"), 1, ap);
    return;
  }

  /* streaming protocol */
  if (listen(sockfd, 5) < 0) {
    iemnet_log(x, IEMNET_ERROR, "unable to listen on socket");
    sys_sockerror("listen");
    iemnet__closesocket(sockfd, 1);
    sockfd = -1;
    outlet_anything(x->x_statusout, gensym("port"), 1, ap);
    return;
  } else {
    /* wait for new connections */
    sys_addpollfn(sockfd,
                  (t_fdpollfn)tcpreceive_connectpoll,
                  x);
  }

  x->x_connectsocket = sockfd;
  x->x_port = portno;

  /* find out which port is actually used (useful when assigning "0") */
  if(!getsockname(sockfd, (struct sockaddr *)&server, &serversize)) {
    x->x_port = ntohs(server.sin_port);
  }

  SETFLOAT(ap, x->x_port);
  outlet_anything(x->x_statusout, gensym("port"), 1, ap);
}

static void tcpreceive_serialize(t_tcpreceive *x, t_floatarg doit)
{
  x->x_serialize = doit;
}

static void tcpreceive_free(t_tcpreceive *x)
{
  /* is this ever called? */
  if (x->x_connectsocket >= 0) {
    sys_rmpollfn(x->x_connectsocket);
    iemnet__closesocket(x->x_connectsocket, 1);
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

  x->x_connectsocket = -1;
  x->x_port = -1;
  x->x_nconnections = 0;

  /* clear the connection list */
  for (i = 0; i < MAX_CONNECTIONS; ++i) {
    x->x_connection[i].socket = -1;
    x->x_connection[i].addr = 0L;
    x->x_connection[i].port = 0;
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

  class_addmethod(tcpreceive_class, (t_method)tcpreceive_serialize,
                  gensym("serialize"), A_FLOAT, 0);
  DEBUGMETHOD(tcpreceive_class);
}

IEMNET_INITIALIZER(tcpreceive_setup);

/* end x_net_tcpreceive.c */
