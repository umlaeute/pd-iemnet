/* udpclient.c
 * copyright © 2010-2015 IOhannes m zmölnig, IEM
 */

/*                                                                              */
/* A client for bidirectional communication from within Pd.                     */
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
#include <string.h>

#include <pthread.h>

static t_class *udpclient_class;
static const char objName[] = "udpclient";

typedef struct _udpclient {
  t_object        x_obj;
  t_outlet        *x_msgout;
  t_outlet        *x_addrout;
  t_outlet        *x_connectout;
  t_outlet        *x_statusout;

  t_iemnet_sender*x_sender;
  t_iemnet_receiver*x_receiver;

  int             x_fd; // the socket
  const char     *x_hostname; // address we want to connect to as text
  int             x_connectstate; // 0 = not connected, 1 = connected
  u_short         x_port; // port we're sending to
  u_short         x_sendport; // port we're sending from

  long            x_addr; // address we're connected to as 32bit int

  t_iemnet_floatlist         *x_floatlist;
} t_udpclient;


/* forward declarations */
static void udpclient_receive_callback(void *x, t_iemnet_chunk*);

/* connection handling */

static void *udpclient_doconnect(t_udpclient*x, int subthread)
{
  struct sockaddr_in  server;
  struct hostent      *hp;
  int                 sockfd;
  int                 broadcast = 1;/* nonzero is true */
  memset(&server, 0, sizeof(server));

  if (x->x_sender) {
    iemnet_log(x, IEMNET_ERROR, "already connected");
    return (x);
  }

  /* connect socket using hostname provided in command line */
  hp = gethostbyname(x->x_hostname);
  if (hp == 0) {
    iemnet_log(x, IEMNET_ERROR, "bad host '%s'?", x->x_hostname);
    return (x);
  }
  server.sin_family = AF_INET;

  /* create a socket */
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  DEBUG("send socket %d\n", sockfd);
  if (sockfd < 0) {
    iemnet_log(x, IEMNET_ERROR, "unable to create socket");
    sys_sockerror("socket");
    return (x);
  }

  /* Enable sending of broadcast messages (if hostname is a broadcast address) */
#ifdef SO_BROADCAST
  if( 0 != setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST,
                      (const void *)&broadcast, sizeof(broadcast))) {
    iemnet_log(x, IEMNET_ERROR, "unable to switch to broadcast mode");
    sys_sockerror("setsockopt");
  }
#endif /* SO_BROADCAST */

  if(x->x_sendport>0) {
    server.sin_family = AF_INET;
    server.sin_port = htons(x->x_sendport);
    server.sin_addr.s_addr = INADDR_ANY;
    if (bind(sockfd, (struct sockaddr *) &server, sizeof (server)) < 0) {
      iemnet_log(x, IEMNET_ERROR, "unable to bind with sending port %d (continuing with random port)", x->x_sendport);
      sys_sockerror("bind");
    }
  }

  /* try to connect. */
  /* assign client port number */
  memcpy((char *)&server.sin_addr, (char *)hp->h_addr, hp->h_length);
  server.sin_port = htons(x->x_port);
  DEBUG("connecting to %s:%d", x->x_hostname, x->x_port);

  if (connect(sockfd, (struct sockaddr *) &server, sizeof (server)) < 0) {
    iemnet_log(x, IEMNET_ERROR, "unable to connect to stream socket");
    sys_sockerror("connect");
    iemnet__closesocket(sockfd, 1);
    return (x);
  }
  x->x_fd = sockfd;
  x->x_addr = ntohl(*(long *)hp->h_addr);

  x->x_sender=iemnet__sender_create(sockfd, NULL, NULL, subthread);
  x->x_receiver=iemnet__receiver_create(sockfd, x,
                                        udpclient_receive_callback, subthread);

  x->x_connectstate = 1;
  iemnet__numconnout(x->x_statusout, x->x_connectout, x->x_connectstate);
  return (x);
}

static int udpclient_do_disconnect(t_udpclient *x)
{
  DEBUG("disconnect %x %x", x->x_sender, x->x_receiver);
  if(x->x_receiver) {
    iemnet__receiver_destroy(x->x_receiver, 0);
  }
  x->x_receiver=NULL;
  if(x->x_sender) {
    iemnet__sender_destroy(x->x_sender, 0);
  }
  x->x_sender=NULL;

  x->x_connectstate = 0;
  if (x->x_fd < 0) {
    return 0;
  }
  iemnet__closesocket(x->x_fd, 1);
  x->x_fd = -1;
  return 1;
}
static void udpclient_disconnect(t_udpclient *x) {
  if(!udpclient_do_disconnect(x)) {
    iemnet_log(x, IEMNET_ERROR, "not connected");
  } else {
    iemnet__numconnout(x->x_statusout, x->x_connectout, x->x_connectstate);
  }
}

static void udpclient_connect(t_udpclient *x, t_symbol *hostname,
                              t_floatarg fportno,
			      t_floatarg fsndportno)
{
  if(x->x_fd>=0) {
    udpclient_disconnect(x);
  }
  /* we get hostname and port and pass them on
     to the child thread that establishes the connection */
  x->x_hostname = hostname->s_name;
  x->x_port = fportno;
  x->x_sendport = (fsndportno>0)?fsndportno:0;
  x->x_connectstate = 0;
  udpclient_doconnect(x, 0);
}

/* sending/receiving */
static void udpclient_send(t_udpclient *x, t_symbol *s, int argc,
                           t_atom *argv)
{
  int size=0;
  t_atom output_atom;
  t_iemnet_sender*sender=x->x_sender;

  t_iemnet_chunk*chunk=iemnet__chunk_create_list(argc, argv);
  if(sender && chunk) {
    size=iemnet__sender_send(sender, chunk);
  }
  iemnet__chunk_destroy(chunk);

  SETFLOAT(&output_atom, size);
  outlet_anything( x->x_statusout, gensym("sendbuffersize"), 1,
                   &output_atom);
}

static void udpclient_receive_callback(void*y, t_iemnet_chunk*c)
{
  t_udpclient *x=(t_udpclient*)y;

  if(c) {
    iemnet__addrout(x->x_statusout, x->x_addrout, x->x_addr, x->x_port);
    x->x_floatlist=iemnet__chunk2list(c,
                                      x->x_floatlist); /* gets destroyed in the dtor */
    outlet_list(x->x_msgout, gensym("list"),x->x_floatlist->argc,
                x->x_floatlist->argv);
  } else {
    // disconnected
    DEBUG("disconnected");
    if(x->x_fd >= 0) {
      udpclient_disconnect(x);
    }
  }
}

/* constructor/destructor */

static void *udpclient_new(void)
{
  t_udpclient *x = (t_udpclient *)pd_new(udpclient_class);
  x->x_msgout = outlet_new(&x->x_obj, 0); /* received data */
  x->x_addrout = outlet_new(&x->x_obj, gensym("list"));
  x->x_connectout = outlet_new(&x->x_obj,
                               gensym("float")); /* connection state */
  x->x_statusout = outlet_new(&x->x_obj,
                              0); /* last outlet for everything else */

  x->x_fd = -1;
  x->x_addr = 0L;
  x->x_port = 0;

  x->x_sender=NULL;
  x->x_receiver=NULL;

  x->x_floatlist=iemnet__floatlist_create(1024);

  return (x);
}

static void udpclient_free(t_udpclient *x)
{
  udpclient_do_disconnect(x);
  if(x->x_floatlist) {
    iemnet__floatlist_destroy(x->x_floatlist);
  }
  x->x_floatlist=NULL;
}

IEMNET_EXTERN void udpclient_setup(void)
{
  if(!iemnet__register(objName)) {
    return;
  }
  udpclient_class = class_new(gensym(objName), (t_newmethod)udpclient_new,
                              (t_method)udpclient_free,
                              sizeof(t_udpclient), 0, A_DEFFLOAT, 0);
  class_addmethod(udpclient_class, (t_method)udpclient_connect,
                  gensym("connect")
                  , A_SYMBOL, A_FLOAT, A_DEFFLOAT, 0);
  class_addmethod(udpclient_class, (t_method)udpclient_disconnect,
                  gensym("disconnect"), 0);
  class_addmethod(udpclient_class, (t_method)udpclient_send, gensym("send"),
                  A_GIMME, 0);
  class_addlist(udpclient_class, (t_method)udpclient_send);

  DEBUGMETHOD(udpclient_class);
}


IEMNET_INITIALIZER(udpclient_setup);

/* end of udpclient.c */
