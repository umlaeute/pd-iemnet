/* udpreceive.c
 * copyright © 2010-2015 IOhannes m zmölnig, IEM
 * copyright (c) 2006-2010 Martin Peach
 * copyright (c) Miller Puckette
 */

/*                                                                              */
/* A server for unidirectional communication from within Pd.                    */
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

#define DEBUGLEVEL 1

static const char objName[] = "udpreceive";

#include "iemnet.h"
#include <string.h>

/* ----------------------------- udpreceive ------------------------- */

static t_class *udpreceive_class;

typedef struct _udpreceive {
  t_object x_obj;
  t_outlet*x_msgout;
  t_outlet*x_addrout;
  t_outlet*x_statout;

  int x_fd;
  int x_port;
  t_iemnet_receiver*x_receiver;
  t_iemnet_floatlist*x_floatlist;

  int x_reuseport, x_reuseaddr;
} t_udpreceive;


static void udpreceive_read_callback(void*y, t_iemnet_chunk*c)
{
  t_udpreceive*x=(t_udpreceive*)y;
  if(c) {
    iemnet__addrout(x->x_statout, x->x_addrout, c->addr, c->port);
    /* gets destroyed in the dtor */
    x->x_floatlist=iemnet__chunk2list(c, x->x_floatlist);
    outlet_list(x->x_msgout, gensym("list"), x->x_floatlist->argc,
                x->x_floatlist->argv);
  } else {
    iemnet_log(x, IEMNET_VERBOSE, "nothing received");
  }
}

static int udpreceive_setport(t_udpreceive*x, unsigned short portno)
{
  struct sockaddr_in server;
  socklen_t serversize=sizeof(server);
  int sockfd = x->x_fd;
  int intarg;
  memset(&server, 0, sizeof(server));

  if(x->x_port == portno) {
    iemnet_log(x, IEMNET_VERBOSE, "skipping re-binding to port:%d", portno);
    return 1;
  }

  /* cleanup any open ports */
  if(x->x_receiver) {
    iemnet__receiver_destroy(x->x_receiver, 0);
    x->x_receiver=NULL;
  }
  if(sockfd>=0) {
    iemnet__closesocket(sockfd, 1);
    x->x_fd=-1;
    x->x_port=-1;
  }

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if(sockfd<0) {
    iemnet_log(x, IEMNET_ERROR, "unable to create socket");
    sys_sockerror("socket");
    return 0;
  }

  /* ask OS to allow another Pd to reopen this port after we close it. */
#ifdef SO_REUSEADDR
  if(x->x_reuseaddr) {
    intarg = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
                   (void *)&intarg, sizeof(intarg))
        < 0) {
      iemnet_log(x, IEMNET_ERROR, "unable to enable address re-using");
      sys_sockerror("setsockopt:SO_REUSEADDR");
    }
  }
#endif /* SO_REUSEADDR */
#ifdef SO_REUSEPORT
  if(x->x_reuseport) {
    intarg = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT,
                   (void *)&intarg, sizeof(intarg))
        < 0) {
      iemnet_log(x, IEMNET_ERROR, "unable to enable port re-using");
      sys_sockerror("setsockopt:SO_REUSEPORT");
    }
  }
#endif /* SO_REUSEPORT */

  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons((u_short)portno);

  /* name the socket */
  if (bind(sockfd, (struct sockaddr *)&server, serversize) < 0) {
    iemnet_log(x, IEMNET_ERROR, "unable to bind to socket");
    sys_sockerror("bind");
    iemnet__closesocket(sockfd, 1);
    sockfd = -1;
    return 0;
  }

  x->x_fd = sockfd;
  x->x_port = portno;

  /* find out which port is actually used (useful when assigning "0") */
  if(!getsockname(sockfd, (struct sockaddr *)&server, &serversize)) {
    x->x_port=ntohs(server.sin_port);
  }

  x->x_receiver=iemnet__receiver_create(sockfd,
                                        x,
                                        udpreceive_read_callback,
                                        0);
  return 1;
}

static void udpreceive_port(t_udpreceive*x, t_symbol*s, int argc,
                            t_atom*argv)
{
  t_atom ap[1];
  if(argc) {
    if(argc>1 || A_FLOAT != argv->a_type) {
      iemnet_log(x, IEMNET_ERROR, "usage: %s [<portnum>]", s->s_name);
      return;
    }
    SETFLOAT(ap, -1);
    if(!udpreceive_setport(x, atom_getint(argv))) {
      outlet_anything(x->x_statout, gensym("port"), 1, ap);
    }
  }

  SETFLOAT(ap, x->x_port);
  outlet_anything(x->x_statout, gensym("port"), 1, ap);
}

static void udpreceive_optionI(t_udpreceive*x, t_symbol*s, int argc,
                               t_atom*argv)
{
  int*reuse=NULL;
  if(gensym("reuseport")==s) {
    reuse=&x->x_reuseport;
  }
  if(gensym("reuseaddr")==s) {
    reuse=&x->x_reuseaddr;
  }

  if(!reuse) {
    iemnet_log(x, IEMNET_ERROR, "unknown option '%s'", s->s_name);
    return;
  }
  if(argc) {
    if(1==argc && A_FLOAT == argv->a_type) {
      *reuse=atom_getint(argv);
      return;
    } else {
      iemnet_log(x, IEMNET_ERROR, "usage: %s [<val>]", s->s_name);
      return;
    }
  } else {
    t_atom ap[1];
    SETFLOAT(ap, *reuse);
    outlet_anything(x->x_statout, s, 1, ap);
  }
}

static void *udpreceive_new(t_floatarg fportno)
{
  t_udpreceive*x = (t_udpreceive *)pd_new(udpreceive_class);

  x->x_msgout = outlet_new(&x->x_obj, 0);
  x->x_addrout = outlet_new(&x->x_obj, gensym("list"));
  x->x_statout = outlet_new(&x->x_obj, 0);

  x->x_fd = -1;
  x->x_port = -1;
  x->x_receiver = NULL;

  x->x_floatlist=iemnet__floatlist_create(1024);

  x->x_reuseaddr = 1;
  x->x_reuseport = 0;

  udpreceive_setport(x, fportno);

  return (x);
}

static void udpreceive_free(t_udpreceive *x)
{
  if(x->x_receiver) {
    iemnet__receiver_destroy(x->x_receiver, 0);
  }
  x->x_receiver=NULL;
  if(x->x_fd >= 0) {
    iemnet__closesocket(x->x_fd, 0);
  }
  x->x_fd=-1;

  outlet_free(x->x_msgout);
  outlet_free(x->x_addrout);
  outlet_free(x->x_statout);

  if(x->x_floatlist) {
    iemnet__floatlist_destroy(x->x_floatlist);
  }
  x->x_floatlist=NULL;
}

IEMNET_EXTERN void udpreceive_setup(void)
{
  if(!iemnet__register(objName)) {
    return;
  }
  udpreceive_class = class_new(gensym(objName),
                               (t_newmethod)udpreceive_new, (t_method)udpreceive_free,
                               sizeof(t_udpreceive), 0, A_DEFFLOAT, 0);

  class_addmethod(udpreceive_class, (t_method)udpreceive_port,
                  gensym("port"), A_GIMME, 0);

  /* options for opening new sockets */
  class_addmethod(udpreceive_class, (t_method)udpreceive_optionI,
                  gensym("reuseaddr"), A_GIMME, 0);
  class_addmethod(udpreceive_class, (t_method)udpreceive_optionI,
                  gensym("reuseport"), A_GIMME, 0);

  DEBUGMETHOD(udpreceive_class);
}

IEMNET_INITIALIZER(udpreceive_setup);

/* end udpreceive.c */
