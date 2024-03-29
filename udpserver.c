/* udpserver.c
 *
 * listens on a UDP-socket for bi-directional communication
 *
 * copyright © 2010-2015 IOhannes m zmölnig, IEM
 * copyright (c) 2006-2010 Martin Peach
 * copyright (c) 2004 Olaf Matthes
 */

/*                                                                              */
/* A server for bidirectional communication from within Pd.                     */
/* Allows to send back data to specific clients connected to the server.        */
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

/* ---------------------------------------------------------------------------- */
#define DEBUGLEVEL 1
#include "iemnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CONNECT 32 /* maximum number of connections */

/* ----------------------------- udpserver ------------------------- */

static t_class *udpserver_class;
static const char objName[] = "udpserver";

typedef struct _udpserver_sender {
  struct _udpserver *sr_owner;

  long sr_host;
  unsigned short sr_port;
  t_symbol*sr_hostname;
  int sr_uniq;
  t_iemnet_sender*sr_sender;

  double sr_lastseen;
} t_udpserver_sender;

typedef struct _udpserver {
  t_object x_obj;
  t_outlet*x_msgout;
  t_outlet*x_connectout;
  t_outlet*x_sockout; /* legacy */
  t_outlet*x_addrout; /* legacy */
  t_outlet*x_statusout;

  t_udpserver_sender**x_sr; /* socket per connection */
  unsigned int x_nconnections;
  unsigned int x_maxconnections;

  int x_connectsocket; /* socket waiting for new connections */
  unsigned short x_port; /* port we are bound to */
  t_symbol*x_ifaddr; /* interface we are bound to */
  unsigned char x_accept; /* whether we accept new connections or not */
  double x_timeout; /* timeout after which clients expire */
  double x_lastchecked; /* when the last timeout check was performed */

  /* the default connection to send to;
     0 = broadcast; >0 use this client; <0 exclude this client
  */
  int x_defaulttarget;

  t_iemnet_receiver*x_receiver;
  t_iemnet_floatlist*x_floatlist;
} t_udpserver;

/* called from:
   - udpserver_sender_copy().
   - udpserver_sender_add()
     - udpserver_receive_callback()
   - udpserver_connectpoll().
*/
static t_udpserver_sender *udpserver_sender_new(t_udpserver *owner,
    unsigned long host, unsigned short port)
{
  static int uniq=1000;
  t_udpserver_sender *x = (t_udpserver_sender *)malloc(sizeof(*x));
  if(NULL == x) {
    iemnet_log(owner, IEMNET_FATAL, "unable to allocate %d bytes to create sender", (int)sizeof(*x));
    return NULL;
  } else {
    char hostname[MAXPDSTRING];
    /* yikes: IPv4 only for now */
    snprintf(hostname, MAXPDSTRING-1, "%d.%d.%d.%d",
             (unsigned char)((host & 0xFF000000)>>24),
             (unsigned char)((host & 0x0FF0000)>>16),
             (unsigned char)((host & 0x0FF00)>>8),
             (unsigned char)((host & 0x0FF))
      );
    hostname[MAXPDSTRING-1] = 0;

    x->sr_owner = owner;

    x->sr_uniq = uniq++;

    x->sr_host = host; //ntohl(addr->sin_addr.s_addr);
    x->sr_port = port; //ntohs(addr->sin_port);
    x->sr_hostname = gensym(hostname);

    x->sr_sender = iemnet__sender_create(owner->x_connectsocket, NULL, NULL, 0);

    x->sr_lastseen = clock_getlogicaltime();
  }
  return (x);
}

static void udpserver_sender_free(t_udpserver_sender *x)
{
  DEBUG("freeing %x", x);
  if (x != NULL) {
    int sockfd = x->sr_uniq;
    t_iemnet_sender*sender = x->sr_sender;

    x->sr_owner = NULL;
    x->sr_sender = NULL;
    x->sr_uniq = -1;

    free(x);

    if(sender) {
      iemnet__sender_destroy(sender, 0);
    }
    if(sockfd >= 0) {
      iemnet__closesocket(sockfd, 1);
    }
  }
  /* coverity[pass_freed_arg]: this is merely for debugging printout */
  DEBUG("freed %x", x);
}

static int udpserver_socket2index(t_udpserver*x, int sockfd)
{
  unsigned int i = 0;
  for(i = 0; i < x->x_nconnections; i++) { /* check if connection exists */
    if(x->x_sr[i]->sr_uniq == sockfd) {
      return i;
    }
  }
  return -1;
}

/* checks whether client is a valid (1-based) index
 *  if the id is invalid, returns -1
 *  if the id is valid, return the 0-based index (client-1)
 */
static int udpserver_fixindex(t_udpserver*x, int client_)
{
  unsigned int client;
  if(client_<1) {
    iemnet_log(x, IEMNET_ERROR,
               "client:%d out of range [1..%d]",
               client_, (int)(x->x_nconnections));
    return -1;
  }
  client = (unsigned int)client_;
  if(x->x_nconnections <= 0) {
    iemnet_log(x, IEMNET_ERROR, "no clients connected");
    return -1;
  }

  if (client > x->x_nconnections) {
    iemnet_log(x, IEMNET_ERROR,
               "client:%d out of range [1..%d]",
               client, (int)(x->x_nconnections));
    return -1;
  }
  return (client-1);
}


/* returns 1 if addr1 == addr2, 0 otherwise */
static int equal_addr(unsigned long host1, unsigned short port1,
                      unsigned long host2, unsigned short port2)
{
  return (
           ((port1 == port2) &&
            (host1 == host2))
         );
}

/* called from:
 * - udpserver_sender_add()
 */
static int udpserver__find_sender(t_udpserver*x, unsigned long host,
                                  unsigned short port)
{
  unsigned int i = 0;
  for(i = 0; i<x->x_nconnections; i++) {
    if(NULL == x->x_sr[i]) {
      return -1;
    }
    if(equal_addr(host, port, x->x_sr[i]->sr_host, x->x_sr[i]->sr_port)) {
      return i;
    }
  }
  return -1;
}

/**
 * check whether the sender is already registered
 * if not, add it to the list of registered senders
 */
/* called from:
 * - udpserver_receive_callback()
 */
static t_udpserver_sender* udpserver_sender_add(t_udpserver*x,
    unsigned long host, unsigned short port )
{
  int id = -1;

  if(!x->x_accept) {
    return NULL;
  }

  id = udpserver__find_sender(x, host, port);
  DEBUG("%X:%d -> %d", host, port, id);
  if(id<0) {
#if 0
    /* since udp is a connection-less protocol we have no way of knowing the currently connected clients
     * the following 3 lines assume, that there is only one client connected (the last we got data from
     */
    id = 0;
    if (x->x_sr[id] == NULL) {
      x->x_sr[id] = udpserver_sender_new(x, host, port);
    } else {
      x->x_sr[id]->sr_port = port;
      x->x_sr[id]->sr_host = host;
    }

    x->x_nconnections = 1;
#else
    /* this is a more optimistic approach as above:
     * every client that we received data from is added to the list of receivers
     * the idea is to remove the sender, if it's known to not receive any data
     */
    id = x->x_nconnections;
    /* an unknown address! add it */
    if(id < (int)x->x_maxconnections) {
      x->x_sr[id] = udpserver_sender_new(x, host, port);
      DEBUG("new sender[%d] = %x", id, x->x_sr[id]);
      x->x_nconnections++;
    } else {
      /* oops, no more senders! */
      id = -1;
    }
#endif
  } else {
    x->x_sr[id]->sr_lastseen = clock_getlogicaltime();
  }
  DEBUG("sender_add: %d", id);
  if(id >= 0) {
    return x->x_sr[id];
  }

  return NULL;
}

static void udpserver_sender_remove(t_udpserver*x, unsigned int id)
{
  if(id<x->x_nconnections && x->x_sr[id]) {
    unsigned int i;

    t_udpserver_sender* sdr = x->x_sr[id];
    udpserver_sender_free(sdr);

    /* close the gap by shifting the remaining connections to the left */
    for(i = id; i<x->x_nconnections; i++) {
      x->x_sr[id] = x->x_sr[id+1];
    }
    x->x_sr[id] = NULL;

    x->x_nconnections--;
  }
}

static void udpserver_sender_autoremove(t_udpserver*x)
{
  /* remove all clients that have timedout */
  unsigned int id;
  t_udpserver_sender**sr=x->x_sr;
  if(x->x_timeout<=0.) {
    return;
  }
  if(clock_gettimesince(x->x_lastchecked) <= 0.) {
    return;
  }
  x->x_lastchecked = clock_getlogicaltime();
  for(id=0; id<x->x_nconnections; id++) {
    if(!x->x_sr[id]) {
      continue;
    }
    if (clock_gettimesince(x->x_sr[id]->sr_lastseen) > x->x_timeout) {
      udpserver_sender_free(x->x_sr[id]);
      x->x_sr[id] = NULL;
      continue;
    }
    *sr++ = x->x_sr[id];
  }
  while(sr < (x->x_sr+x->x_maxconnections)) {
    *sr++=NULL;
  }
  x->x_nconnections = 0;
  for(id=0; id<x->x_maxconnections; id++) {
    if(!x->x_sr[id]) {
      break;
    }
    x->x_nconnections++;
  }
}


/* ---------------- udpserver info ---------------------------- */
static void udpserver_info_client(t_udpserver *x, unsigned int client)
{
  /*
     "client <id> <socket> <IP> <port>"
     "bufsize <id> <insize> <outsize>"
  */
  static t_atom output_atom[5];
  if(x && client<x->x_maxconnections && x->x_sr[client]) {
    unsigned short port = x->x_sr[client]->sr_port;

    int insize = iemnet__receiver_getsize(x->x_receiver);
    int outsize = iemnet__sender_getsize(x->x_sr[client]->sr_sender);

    SETFLOAT(output_atom+0, client+1);
    SETSYMBOL(output_atom+1, gensym("address"));

    SETSYMBOL(output_atom+2, x->x_sr[client]->sr_hostname);
    SETFLOAT(output_atom+3, port);
    SETFLOAT(output_atom+4, clock_gettimesince(x->x_sr[client]->sr_lastseen));

    outlet_anything( x->x_statusout, gensym("client"), 5, output_atom);

    SETFLOAT(output_atom+0, client+1);
    SETFLOAT(output_atom+1, insize);
    SETFLOAT(output_atom+2, outsize);
    outlet_anything( x->x_statusout, gensym("bufsize"), 3, output_atom);
  }
}


static void udpserver_info(t_udpserver *x)
{
  static t_atom output_atom[4];
  int sockfd = x->x_connectsocket;

  int port = x->x_port;

  if(sockfd<0) {
    /* no open port */
    iemnet_log(x, IEMNET_ERROR, "no open socket");
  }

  udpserver_sender_autoremove(x);

  if(x->x_port <= 0) {
    struct sockaddr_in server;
    socklen_t serversize = sizeof(server);
    memset(&server, 0, sizeof(server));

    if(!getsockname(sockfd, (struct sockaddr *)&server, &serversize)) {
      x->x_port = ntohs(server.sin_port);
      port = x->x_port;
    } else {
      iemnet_log(x, IEMNET_ERROR, "getsockname failed for socket:%d", sockfd);
      sys_sockerror("getsockname");
    }
  }


  iemnet__socket2addressout(sockfd, x->x_statusout, gensym("local_address"));

  SETFLOAT(output_atom+0, port);
  outlet_anything( x->x_statusout, gensym("port"), 1, output_atom);

  SETFLOAT(output_atom+0, x->x_nconnections);
  outlet_anything( x->x_statusout, gensym("connections"), 1, output_atom);
}


static void udpserver_info_connection(t_udpserver *x, t_udpserver_sender*y)
{
  iemnet__addrout(x->x_statusout, x->x_addrout, y->sr_host, y->sr_port);
  //outlet_float(x->x_sockout, y->sr_uniq);
}

/* ---------------- main udpserver (send) stuff --------------------- */
static void udpserver_disconnect(t_udpserver *x, unsigned int client);
static void udpserver_send_bytes(t_udpserver*x, unsigned int client,
                                 t_iemnet_chunk*chunk)
{
  DEBUG("send_bytes to %x -> %x[%d]", x, x->x_sr, client);
  if(client<x->x_maxconnections) {
    DEBUG("client %X", x->x_sr[client]);
  }
  if(x && client<x->x_maxconnections && x->x_sr[client]) {
    t_atom output_atom[3];
    int size = 0;

    t_iemnet_sender*sender = x->x_sr[client]->sr_sender;
    int sockfd = x->x_sr[client]->sr_uniq;

    chunk->addr = x->x_sr[client]->sr_host;
    chunk->port = x->x_sr[client]->sr_port;

    if(sender) {
      size = iemnet__sender_send(sender, chunk);
    }

    SETFLOAT(&output_atom[0], client+1);
    SETFLOAT(&output_atom[1], size);
    SETFLOAT(&output_atom[2], sockfd);
    outlet_anything( x->x_statusout, gensym("sendbuffersize"), 3, output_atom);

    if(size<0) {
      /* disconnected! */
      udpserver_disconnect(x, client);
    }
  }
}


/* broadcasts a message to all connected clients but the given one */
static void udpserver_send_butclient(t_udpserver *x, unsigned int but,
                                     int argc, t_atom *argv)
{
  unsigned int client = 0;
  t_iemnet_chunk*chunk = iemnet__chunk_create_list(argc, argv);

  /* enumerate through the clients and send each the message */
  for(client = 0; client < x->x_nconnections;
      client++) {	/* check if connection exists */
    /* socket exists for this client */
    if(client != but) {
      udpserver_send_bytes(x, client, chunk);
    }
  }
  iemnet__chunk_destroy(chunk);
}
/* sends a message to a given client */
static void udpserver_send_toclient(t_udpserver *x, unsigned int client,
                                    int argc, t_atom *argv)
{
  t_iemnet_chunk*chunk = iemnet__chunk_create_list(argc, argv);
  udpserver_send_bytes(x, client, chunk);
  iemnet__chunk_destroy(chunk);
}

/* send message to client using client number
   note that the client numbers might change in case a client disconnects! */
/* clients start at 1 but our index starts at 0 */
static void udpserver_send_client(t_udpserver *x, t_symbol *s, int argc,
                                  t_atom *argv)
{
  unsigned int client = 0;
  (void)s; /* ignore unused variable */

  if (argc > 0) {
    int c = udpserver_fixindex(x, atom_getint(argv));
    if(c<0) {
      return;
    }
    client = (unsigned int)c;
    if(argc == 1) {
      udpserver_info_client(x, client);
    } else {
      udpserver_send_toclient(x, client, argc-1, argv+1);
    }
    return;
  } else {
    for(client = 0; client<x->x_nconnections; client++) {
      udpserver_info_client(x, client);
    }
  }
}

/* broadcasts a message to all connected clients */
static void udpserver_broadcast(t_udpserver *x, t_symbol *s, int argc,
                                t_atom *argv)
{
  unsigned int client;
  unsigned int oldconnections = x->x_nconnections;
  t_iemnet_chunk*chunk = iemnet__chunk_create_list(argc, argv);
  (void)s; /* ignore unused variable */
  udpserver_sender_autoremove(x);

  DEBUG("broadcasting to %d clients", x->x_nconnections);

  /* enumerate through the clients and send each the message */
  for(client = 0; client < x->x_nconnections;
      client++) {	/* check if connection exists */
    /* socket exists for this client */
    udpserver_send_bytes(x, client, chunk);
  }
  iemnet__chunk_destroy(chunk);
  if(oldconnections != x->x_nconnections) {
    t_atom a[1];
    SETFLOAT(a+0, x->x_nconnections);
    outlet_anything( x->x_statusout, gensym("connections"), 1, a);
  }
}

static void udpserver_defaultsend(t_udpserver *x, t_symbol *s, int argc,
                                  t_atom *argv)
{
  int client = -1;
  int sockfd = x->x_defaulttarget;
  DEBUG("sending to sockfd: %d", sockfd);
  if(sockfd>0) {
    client = udpserver_socket2index(x, sockfd);
    if(client<0) {
      iemnet_log(x, IEMNET_ERROR,
                 "invalid socket %d, switching to broadcast mode",
                 sockfd);
      x->x_defaulttarget = 0;
    } else {
      udpserver_send_toclient(x, client, argc, argv);
      return;
    }
  } else if(sockfd<0) {
    client = udpserver_socket2index(x, -sockfd);
    if(client<0) {
      iemnet_log(x, IEMNET_ERROR,
                 "invalid excluded socket %d, switching to broadcast mode",
                 -sockfd);
      x->x_defaulttarget = 0;
    } else {
      udpserver_send_butclient(x, client, argc, argv);
      return;
    }
  }

  udpserver_broadcast(x, s, argc, argv);
}
static void udpserver_defaulttarget(t_udpserver *x, t_floatarg f)
{
  int sockfd = 0;
  int rawclient = f;
  unsigned int client = (rawclient<0)?(-rawclient):rawclient;

  if(client > x->x_nconnections) {
    iemnet_log(x, IEMNET_ERROR,
               "target:%d out of range [0..%d]",
               client, (int)(x->x_nconnections));
    return;
  }

  /* map the client to a persistant socket */
  if(client>0) {
    sockfd = x->x_sr[client-1]->sr_uniq;
  }

  if(rawclient<0) {
    sockfd = -sockfd;
  }

  x->x_defaulttarget = sockfd;
}
static void udpserver_add_client(t_udpserver *x, t_symbol*s, t_float f)
{
  struct hostent*hp;
  struct sockaddr_in server;
  unsigned int port = (unsigned int)f;
  if((int)f<1 || (int)f>=0xFFFF) {
    iemnet_log(x, IEMNET_ERROR, "bad port '%d'?", (int)f);
    return;
  }
  hp = gethostbyname(s->s_name);
  if (hp == 0) {
    iemnet_log(x, IEMNET_ERROR, "bad host '%s'?", s->s_name);
    return;
  }

  server.sin_family = AF_INET;
  memcpy((char *)&server.sin_addr, (char *)hp->h_addr, hp->h_length);
  if (!udpserver_sender_add(x, ntohl(server.sin_addr.s_addr), port)) {
    iemnet_log(x, IEMNET_ERROR, "unable to add client %s:%d", s->s_name, port);
  }
}



/* disconnect client */
static void udpserver_disconnect(t_udpserver *x, unsigned int client)
{
  t_udpserver_sender*sdr = NULL;
  int conns;
  DEBUG("disconnect %x %d", x, client);

  if(client >= x->x_nconnections) {
    return;
  }

  sdr = (t_udpserver_sender *)calloc(1, sizeof(*sdr));
  if(sdr) {
    sdr->sr_host = x->x_sr[client]->sr_host;
    sdr->sr_port = x->x_sr[client]->sr_port;
  }

  udpserver_sender_remove(x, client);
  udpserver_sender_autoremove(x);
  conns = x->x_nconnections;

  if(sdr) {
    udpserver_info_connection(x, sdr);
    free(sdr);
  }
  iemnet__numconnout(x->x_statusout, x->x_connectout, conns);
}

/* disconnect a client by number */
static void udpserver_disconnect_client(t_udpserver *x, t_floatarg fclient)
{
  int client = udpserver_fixindex(x, fclient);

  if(client<0) {
    return;
  }
  udpserver_disconnect(x, client);
}

/* disconnect all clients */
static void udpserver_disconnect_all(t_udpserver *x)
{
  unsigned int id;
  for(id = 0; id<x->x_nconnections; id++) {
    udpserver_disconnect(x, id);
  }
}

/* whether we should accept new connections */
static void udpserver_accept(t_udpserver *x, t_float f)
{
  x->x_accept = (unsigned char)f;
}
/* set the client timeout (in ms) */
static void udpserver_timeout(t_udpserver *x, t_float f)
{
  x->x_timeout = f;
}

/* ---------------- main udpserver (receive) stuff --------------------- */
/* called from:
   - iemnet_receiver (calling context: main thread)
*/
static void udpserver_receive_callback(void *y, t_iemnet_chunk*c)
{
  t_udpserver*x = (t_udpserver*)y;
  if(NULL == y) {
    return;
  }

  if(c) {
    unsigned int conns = x->x_nconnections;
    t_udpserver_sender*sdr = NULL;
    DEBUG("add new sender from %d", c->port);
    sdr = udpserver_sender_add(x, c->addr, c->port);
    DEBUG("added new sender from %d", c->port);
    if(sdr) {
      udpserver_info_connection(x, sdr);
      /* gets destroyed in the dtor */
      x->x_floatlist = iemnet__chunk2list(c, x->x_floatlist);

      /* here we might have a reentrancy problem */
      if(conns != x->x_nconnections) {
        iemnet__numconnout(x->x_statusout, x->x_connectout, x->x_nconnections);
      }
      outlet_list(x->x_msgout, gensym("list"), x->x_floatlist->argc,
                  x->x_floatlist->argv);
    }
  } else {
    /* disconnection never happens with a connectionless protocol like UDP */
    iemnet_log(x, IEMNET_ERROR, "received disconnection event");
  }
}



static void udpserver_do_bind(t_udpserver*x, t_symbol*ifaddr, unsigned short portno)
{
  static t_atom ap[1];
  struct sockaddr_in server;
  socklen_t serversize = sizeof(server);
  int sockfd = x->x_connectsocket;
  memset(&server, 0, sizeof(server));

  SETFLOAT(ap, -1);
  if(x->x_port == portno && x->x_ifaddr == ifaddr) {
    return;
  }

  /* cleanup any open ports */
  if(sockfd >= 0) {
    //sys_rmpollfn(sockfd);
    iemnet__closesocket(sockfd, 0);
    x->x_connectsocket = -1;
    x->x_port = -1;
    x->x_ifaddr = 0;
  }


  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if(sockfd<0) {
    iemnet_log(x, IEMNET_ERROR, "unable to create socket");
    sys_sockerror("socket");
    return;
  }

  server.sin_family = AF_INET;

  /* LATER allow setting of inaddr */
  if (ifaddr) {
    struct hostent *hp = gethostbyname(ifaddr->s_name);
    if(!hp) {
      iemnet_log(x, IEMNET_ERROR, "bad host '%s'?", ifaddr->s_name);
      iemnet__closesocket(sockfd, 0);
      return;
    }
    memcpy((char *)&server.sin_addr, (char *)hp->h_addr, hp->h_length);
  } else {
    server.sin_addr.s_addr = INADDR_ANY;
  }


  /* assign server port number */
  server.sin_port = htons((u_short)portno);
  /* name the socket */
  if (bind(sockfd, (struct sockaddr *)&server, serversize) < 0) {
    iemnet_log(x, IEMNET_ERROR, "unable to bind socket to %s:%d", ifaddr?ifaddr->s_name:"*", portno);
    sys_sockerror("bind");
    iemnet__closesocket(sockfd, 1);
    outlet_anything(x->x_statusout, gensym("port"), 1, ap);
    return;
  }

  x->x_receiver = iemnet__receiver_create(sockfd,
                                          x,
                                          udpserver_receive_callback,
                                          0);
  x->x_connectsocket = sockfd;
  x->x_port = portno;
  x->x_ifaddr = ifaddr;

  /* find out which port is actually used (useful when assigning "0") */
  if(!getsockname(sockfd, (struct sockaddr *)&server, &serversize)) {
    x->x_port = ntohs(server.sin_port);
  }

  iemnet__socket2addressout(sockfd, x->x_statusout, gensym("local_address"));

  SETFLOAT(ap, x->x_port);
  outlet_anything(x->x_statusout, gensym("port"), 1, ap);
}

static void udpserver_port(t_udpserver*x, t_floatarg fportno)
{
  if(fportno<0 || (int)fportno >= 0xFFFF) {
    pd_error(x, "[%s] port %d out of range", objName, (int)fportno);
    return;
  }
  udpserver_do_bind(x, 0, (unsigned short)fportno);
}
static void udpserver_bind(t_udpserver*x, t_symbol*s, int argc, t_atom*argv)
{
  unsigned short port = x->x_port;
  (void)s; /* ignore unused variable */
  switch (argc) {
  default:
    return;
  case 2: { /* address, port */
    t_float fportno = atom_getfloat(argv+1);
    if(fportno<0 || (int)fportno > 0xFFFF) {
      pd_error(x, "[%s] port %d out of range", objName, (int)fportno);
      return;
    }
    port = (unsigned short)fportno;
  }
  /* fall through */
  case 1: { /* address */
    t_symbol*ifaddr = (A_FLOAT == argv->a_type)?0:atom_getsymbol(argv+0);
    udpserver_do_bind(x, ifaddr, port);
  }
    break;
  }
}

static void udpserver_maxconnections(t_udpserver *x, t_floatarg maxconnf)
{
  unsigned int maxconn = (unsigned int)maxconnf;
  t_udpserver_sender**sr;
  if(maxconnf<1) {
    pd_error(x, "maximum number of connections must be > 0");
    return;
  }
  if(maxconn < x->x_nconnections) {
    pd_error(x, "maximum number of connections < number of currently connected clients [%d]", x->x_nconnections);
    return;
  }
  if(maxconn == x->x_maxconnections) {
    return;
  }

  sr = resizebytes(x->x_sr
      , sizeof(*sr) * x->x_maxconnections
      , sizeof(*sr) * maxconn
      );
  if(sr) {
    x->x_sr = sr;
    x->x_maxconnections = maxconn;
  } else {
    pd_error(x, "failed to set maximum number of connections");
  }
}

static void *udpserver_new(t_floatarg fportno)
{
  t_udpserver*x;
  unsigned int i;

  x = (t_udpserver *)pd_new(udpserver_class);

  x->x_msgout = outlet_new(&x->x_obj, 0); /* 1st outlet for received data */
  x->x_connectout = outlet_new(&x->x_obj,
                               gensym("float")); /* 2nd outlet for number of connected clients */
  x->x_sockout = outlet_new(&x->x_obj, gensym("float"));
  x->x_addrout = outlet_new(&x->x_obj, gensym("list" ));
  /* 5th outlet for everything else */
  x->x_statusout = outlet_new(&x->x_obj, 0);

  x->x_connectsocket = -1;
  x->x_port = -1;
  x->x_nconnections = 0;
  x->x_maxconnections = MAX_CONNECT;
  x->x_sr = (t_udpserver_sender**)getbytes(sizeof(*x->x_sr) * x->x_maxconnections);

  for(i = 0; i < x->x_maxconnections; i++) {
    x->x_sr[i] = NULL;
  }

  x->x_defaulttarget = 0;
  x->x_floatlist = iemnet__floatlist_create(1024);

  udpserver_port(x, fportno);

  x->x_accept = 1;

  return (x);
}

static void udpserver_free(t_udpserver *x)
{
  unsigned int i;
  for(i = 0; i < x->x_maxconnections; i++) {
    if (NULL != x->x_sr[i]) {
      DEBUG("[%s] free %x", objName, x);
      udpserver_sender_free(x->x_sr[i]);
      x->x_sr[i] = NULL;
    }
  }
  freebytes(x->x_sr, sizeof(*x->x_sr) * x->x_maxconnections);
  x->x_sr = NULL;

  if(x->x_receiver) {
    iemnet__receiver_destroy(x->x_receiver, 0);
    x->x_receiver = NULL;
  }
  if (x->x_connectsocket >= 0) {
    iemnet__closesocket(x->x_connectsocket, 0);
    x->x_connectsocket = -1;
  }
  if(x->x_floatlist) {
    iemnet__floatlist_destroy(x->x_floatlist);
    x->x_floatlist = NULL;
  }
}

IEMNET_EXTERN void udpserver_setup(void)
{
  if(!iemnet__register(objName)) {
    return;
  }

  udpserver_class = class_new(gensym(objName),(t_newmethod)udpserver_new,
                              (t_method)udpserver_free,
                              sizeof(t_udpserver), 0, A_DEFFLOAT, 0);
  class_addmethod(udpserver_class, (t_method)udpserver_disconnect_client,
                  gensym("disconnectclient"), A_DEFFLOAT, 0);
  class_addmethod(udpserver_class, (t_method)udpserver_disconnect_all,
                  gensym("disconnect"), 0);

  class_addmethod(udpserver_class, (t_method)udpserver_accept,
                  gensym("accept"), A_FLOAT, 0);
  class_addmethod(udpserver_class, (t_method)udpserver_maxconnections,
                  gensym("maxconnections"), A_FLOAT, 0);
  class_addmethod(udpserver_class, (t_method)udpserver_timeout,
                  gensym("timeout"), A_FLOAT, 0);

  class_addmethod(udpserver_class, (t_method)udpserver_send_client,
                  gensym("client"), A_GIMME, 0);
  class_addmethod(udpserver_class, (t_method)udpserver_broadcast,
                  gensym("broadcast"), A_GIMME, 0);
  class_addmethod(udpserver_class, (t_method)udpserver_defaultsend,
                  gensym("send"), A_GIMME, 0);
  class_addlist(udpserver_class, (t_method)udpserver_defaultsend);

  class_addmethod(udpserver_class, (t_method)udpserver_defaulttarget,
                  gensym("target"), A_DEFFLOAT, 0);
  class_addmethod(udpserver_class, (t_method)udpserver_add_client,
                  gensym("addclient"), A_SYMBOL, A_FLOAT, 0);

  class_addmethod(udpserver_class, (t_method)udpserver_bind, gensym("bind"),
                  A_GIMME, 0);
  class_addmethod(udpserver_class, (t_method)udpserver_port, gensym("port"),
                  A_DEFFLOAT, 0);
  class_addbang(udpserver_class, (t_method)udpserver_info);

  DEBUGMETHOD(udpserver_class);
}

IEMNET_INITIALIZER(udpserver_setup);


/* end of udpserver.c */
