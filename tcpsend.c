/* tcpsend.c
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

static const char objName[] = "tcpsend";

#include "iemnet.h"
#include <string.h>

#ifndef _WIN32
# include <netinet/tcp.h>
#endif

static t_class *tcpsend_class;

typedef struct _tcpsend {
  t_object x_obj;
  int x_fd;
  t_float x_timeout;
  t_iemnet_sender*x_sender;
} t_tcpsend;

static void tcpsend_disconnect(t_tcpsend *x)
{
  if(x->x_sender) {
    iemnet__sender_destroy(x->x_sender, 0);
  }
  x->x_sender = NULL;
  if (x->x_fd >= 0) {
    iemnet__closesocket(x->x_fd, 1);
    x->x_fd = -1;
    outlet_float(x->x_obj.ob_outlet, 0);
  }
}

static void tcpsend_connect(t_tcpsend *x, t_symbol *hostname,
                            t_floatarg fportno)
{
  int err;
  struct sockaddr_in server;
  struct addrinfo *ailist = NULL, *ai;
  int sockfd = -1;
  int portno = fportno;

  memset(&server, 0, sizeof(server));

  if (x->x_fd >= 0) {
    iemnet_log(x, IEMNET_ERROR, "already connected");
    return;
  }

  /* resolve hostname provided as argument */
  err = iemnet__getaddrinfo(&ailist, hostname->s_name, portno, 0, SOCK_STREAM);
  if (err) {
    iemnet_log(x, IEMNET_ERROR, "%s (%d)\n\tbad host ('%s') or port (%d)?",
               gai_strerror(err), err,
               hostname->s_name, portno);
    return;
  }


  /* try each addr until we find one that works */
  for (ai = ailist; ai != NULL; ai = ai->ai_next) {
    int intarg;

    /* create a socket */
    sockfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    DEBUG("send socket %d\n", sockfd);
    if (sockfd < 0)
      continue;

    /* for stream (TCP) sockets, specify "nodelay" */
    intarg = 1;
    if (setsockopt(sockfd, ai->ai_protocol, TCP_NODELAY,
                   (char *)&intarg, sizeof(intarg)) < 0) {
      iemnet_log(x, IEMNET_ERROR, "unable to enable immediate sending");
      sys_sockerror("setsockopt");
    }
    intarg = 0;
    if (ai->ai_family == AF_INET6 &&
        setsockopt(sockfd, ai->ai_protocol, IPV6_V6ONLY,
                   (char *)&intarg, sizeof(intarg)) < 0) {
      /* post("netreceive: setsockopt (IPV6_V6ONLY) failed"); */
    }

    iemnet_log(x, IEMNET_VERBOSE, "connecting to port %d", portno);
    /* try to connect. */
    if (iemnet__connect(sockfd, ai->ai_addr, ai->ai_addrlen, x->x_timeout) < 0) {
      iemnet_log(x, IEMNET_VERBOSE, "unable to initiate connection on socket %d", sockfd);
      sys_sockerror("connect");
      iemnet__closesocket(sockfd, 1);
      sockfd=-1;
      continue;
    } else {
      break;
    }

  }
  freeaddrinfo(ailist);

  if (sockfd < 0) {
    iemnet_log(x, IEMNET_ERROR, "unable to connect to %s : %d", hostname->s_name, portno);
    sys_sockerror("socket");
    return;
  }

  x->x_fd = sockfd;

  x->x_sender = iemnet__sender_create(sockfd, NULL, NULL, 0);

  outlet_float(x->x_obj.ob_outlet, 1);
}

static void tcpsend_timeout(t_tcpsend *x, t_float timeout)
{
  x->x_timeout = timeout;
}
static void tcpsend_send(t_tcpsend *x, t_symbol *s, int argc, t_atom *argv)
{
  t_iemnet_sender*sender = x->x_sender;
  t_iemnet_chunk*chunk = iemnet__chunk_create_list(argc, argv);
  (void)s; /* ignore unused variable */
  if(sender && chunk) {
    iemnet__sender_send(sender, chunk);
  }
  iemnet__chunk_destroy(chunk);
}

static void tcpsend_free(t_tcpsend *x)
{
  tcpsend_disconnect(x);
}

static void *tcpsend_new(void)
{
  t_tcpsend *x = (t_tcpsend *)pd_new(tcpsend_class);
  outlet_new(&x->x_obj, gensym("float"));
  x->x_fd = -1;
  x->x_timeout = -1;
  return (x);
}

IEMNET_EXTERN void tcpsend_setup(void)
{
  if(!iemnet__register(objName)) {
    return;
  }
  tcpsend_class = class_new(gensym(objName),
                            (t_newmethod)tcpsend_new, (t_method)tcpsend_free,
                            sizeof(t_tcpsend),
                            0, 0);

  class_addmethod(tcpsend_class, (t_method)tcpsend_connect,
                  gensym("connect"), A_SYMBOL, A_FLOAT, 0);
  class_addmethod(tcpsend_class, (t_method)tcpsend_disconnect,
                  gensym("disconnect"), 0);
  class_addmethod(tcpsend_class, (t_method)tcpsend_send, gensym("send"),
                  A_GIMME, 0);
  class_addlist(tcpsend_class, (t_method)tcpsend_send);
  class_addmethod(tcpsend_class, (t_method)tcpsend_timeout, gensym("timeout"),
                  A_FLOAT, 0);

  DEBUGMETHOD(tcpsend_class);
}

IEMNET_INITIALIZER(tcpsend_setup);

/* end tcpsend.c */
