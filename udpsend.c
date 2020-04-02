/* udpsend.c
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

static const char objName[] = "udpsend";

#include "iemnet.h"
#include <string.h>

static t_class *udpsend_class;

typedef struct _udpsend {
  t_object x_obj;
  t_iemnet_sender*x_sender;
  int x_fd;
} t_udpsend;

static void udpsend_connect(t_udpsend *x, t_symbol *hostname,
                            t_floatarg fportno)
{
  struct sockaddr_in server;
  struct hostent*hp = NULL;
  int sockfd;
  int portno = fportno;
  int broadcast = 1;/* nonzero is true */
  memset(&server, 0, sizeof(server));

  if (x->x_sender) {
    iemnet_log(x, IEMNET_ERROR, "already connected");
    return;
  }

  /* connect socket using hostname provided in command line */
  server.sin_family = AF_INET;

  hp = gethostbyname(hostname->s_name);
  if (hp == 0) {
    iemnet_log(x, IEMNET_ERROR, "bad host '%s'?", hostname->s_name);
    return;
  }
  memcpy((char *)&server.sin_addr, (char *)hp->h_addr, hp->h_length);

  /* assign client port number */
  server.sin_port = htons((u_short)portno);

  DEBUG("connecting to port %d", portno);


  /* create a socket */
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  DEBUG("send socket %d\n", sockfd);
  if (sockfd < 0) {
    iemnet_log(x, IEMNET_ERROR, "unable to create datagram socket");
    sys_sockerror("socket");
    return;
  }

  /* Enable sending of broadcast messages (if hostname is a broadcast address)*/
#ifdef SO_BROADCAST
  if( 0 != setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST,
                      (const void *)&broadcast, sizeof(broadcast))) {
    iemnet_log(x, IEMNET_ERROR, "unable to switch to broadcast mode");
    sys_sockerror("setsockopt:SO_BROADCAST");
  }
#endif /* SO_BROADCAST */

  /* try to connect. */
  if (connect(sockfd, (struct sockaddr *) &server, sizeof (server)) < 0) {
    iemnet_log(x, IEMNET_ERROR, "unable to connect to socket:%d", sockfd);
    sys_sockerror("connect");
    iemnet__closesocket(sockfd, 1);
    return;
  }
  x->x_sender = iemnet__sender_create(sockfd, NULL, NULL, 0);
  x->x_fd = sockfd;
  outlet_float(x->x_obj.ob_outlet, 1);
}

static void udpsend_disconnect(t_udpsend *x)
{
  if(x->x_sender) {
    iemnet__sender_destroy(x->x_sender, 0);
  }
  x->x_sender = NULL;
  if(x->x_fd >= 0) {
    iemnet__closesocket(x->x_fd, 1);
    x->x_fd = -1;
    outlet_float(x->x_obj.ob_outlet, 0);
  }
}

static void udpsend_send(t_udpsend *x, t_symbol *s, int argc, t_atom *argv)
{
  (void)s; /* ignore unused variable */
  if(x->x_sender) {
    t_iemnet_chunk*chunk = iemnet__chunk_create_list(argc, argv);
    int size = iemnet__sender_send(x->x_sender, chunk);
    iemnet__chunk_destroy(chunk);
    if(size < 1) {
      /* ouch, the "connection" broke */
      udpsend_disconnect(x);
    }
  } else {
    iemnet_log(x, IEMNET_ERROR, "not connected");
  }
}

static void udpsend_free(t_udpsend *x)
{
  udpsend_disconnect(x);
}

static void *udpsend_new(void)
{
  t_udpsend *x = (t_udpsend *)pd_new(udpsend_class);
  outlet_new(&x->x_obj, gensym("float"));
  x->x_sender = NULL;
  x->x_fd = -1;
 return (x);
}

IEMNET_EXTERN void udpsend_setup(void)
{
  if(!iemnet__register(objName)) {
    return;
  }
  udpsend_class = class_new(gensym(objName), (t_newmethod)udpsend_new,
                            (t_method)udpsend_free,
                            sizeof(t_udpsend), 0, 0);

  class_addmethod(udpsend_class, (t_method)udpsend_connect,
                  gensym("connect"), A_SYMBOL, A_FLOAT, 0);
  class_addmethod(udpsend_class, (t_method)udpsend_disconnect,
                  gensym("disconnect"), 0);

  class_addmethod(udpsend_class, (t_method)udpsend_send, gensym("send"),
                  A_GIMME, 0);
  class_addlist(udpsend_class, (t_method)udpsend_send);
  DEBUGMETHOD(udpsend_class);
}

IEMNET_INITIALIZER(udpsend_setup);

/* end udpsend.c*/
