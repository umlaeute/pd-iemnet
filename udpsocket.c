/* udpsocket.c
 * copyright © 2010-2015 IOhannes m zmölnig, IEM
 */

/*                                                                              */
/* A threaded wrapper around socket for datagrams                               */
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

static t_class *udpsocket_class;
static const char objName[] = "udpsocket";

typedef struct _udpsocket {
  t_object x_obj;
  t_outlet*x_msgout;
  t_outlet*x_statusout;

  t_iemnet_sender*x_sender;
  t_iemnet_receiver*x_receiver;

  int x_fd; /* the socket */
  u_short x_port; /* port we're sending from */
  t_symbol*x_ifaddr; /* interface */

  t_iemnet_floatlist*x_floatlist;

  struct addrinfo*x_addrinfo;
} t_udpsocket;



static void udpsocket_info(t_udpsocket *x)
{
  /*
    "server <socket> <IP> <port>"
    "bufsize <insize> <outsize>"
  */
  static t_atom ap[3];
  int sockfd = x->x_fd;
  int insize = x->x_receiver?iemnet__receiver_getsize(x->x_receiver):-1;
  int outsize = x->x_sender?iemnet__sender_getsize(x->x_sender):-1;
  PERTHREAD t_symbol*s_info=0;
  PERTHREAD t_symbol*s_local_address=0;
  PERTHREAD t_symbol*s_bufsize=0;
  if(!s_info)s_info = gensym("info");
  if(!s_local_address)s_local_address = gensym("local_address");
  if(!s_bufsize)s_bufsize = gensym("bufsize");


  if(sockfd >= 0) {
    struct sockaddr_storage address;
    socklen_t addresssize = sizeof(address);
    if (!getsockname(sockfd, (struct sockaddr *) &address, &addresssize)) {
      int port=0;
      SETSYMBOL(ap+0, s_local_address);
      SETSYMBOL(ap+1, iemnet__sockaddr2sym(&address, &port));
      SETFLOAT(ap+2, port);
      outlet_anything(x->x_statusout, s_info, 3, ap);
    }
  }

  SETSYMBOL(ap+0, s_bufsize);
  SETFLOAT(ap+1, insize);
  SETFLOAT(ap+2, outsize);
  outlet_anything(x->x_statusout, s_info, 3, ap);
}


static int udpsocket_do_disconnect(t_udpsocket *x)
{
  DEBUG("disconnect %x %x", x->x_sender, x->x_receiver);
  if(x->x_receiver) {
    iemnet__receiver_destroy(x->x_receiver, 0);
  }
  x->x_receiver = NULL;
  if(x->x_sender) {
    iemnet__sender_destroy(x->x_sender, 0);
  }
  x->x_sender = NULL;

  if (x->x_fd < 0) {
    return 0;
  }
  iemnet__closesocket(x->x_fd, 1);
  x->x_fd = -1;
  return 1;
}


/* sending/receiving */
static void udpsocket_send(t_udpsocket *x, t_symbol *s, int argc,
                           t_atom *argv)
{
  struct addrinfo *rp;
  int size = 0;
  t_atom output_atom;
  t_iemnet_sender*sender = x->x_sender;
  t_iemnet_chunk*chunk = iemnet__chunk_create_list(argc, argv);
  (void)s; /* ignore unused variable */
  if(sender && chunk) {
    for (rp = x->x_addrinfo; rp != NULL; rp = rp->ai_next) {
      memcpy(&chunk->address, rp->ai_addr, sizeof(&rp->ai_addr));
      size = iemnet__sender_send(sender, chunk);
      break;
    }
  }
  iemnet__chunk_destroy(chunk);

  SETFLOAT(&output_atom, size);
  outlet_anything( x->x_statusout, gensym("sendbuffersize"), 1,
                   &output_atom);
}

static void udpsocket_receive_callback(void*y, t_iemnet_chunk*c)
{
  t_udpsocket *x = (t_udpsocket*)y;

  if(c) {
    t_atom a[2];
    int port;
    SETSYMBOL(a+0, iemnet__sockaddr2sym(&c->address, &port));
    SETFLOAT(a+1, port);
    outlet_anything(x->x_statusout, gensym("address"), 2, a);

    //iemnet__addrout(x->x_statusout, 0, &c->address);
    x->x_floatlist = iemnet__chunk2list(c,
                                      x->x_floatlist); /* gets destroyed in the dtor */
    outlet_list(x->x_msgout, gensym("list"),x->x_floatlist->argc,
                x->x_floatlist->argv);
  }
}


static void udpsocket_connect(t_udpsocket*x, t_symbol*s, t_float portf) {
  char port[8];
  struct addrinfo hints;
  if(portf<1 || portf>=0xFFFF) {
    pd_error(x, "port out of range (1..65535)");
    return;
  }
  snprintf(port, 7, "%d", (int)portf);
  port[7]=0;
  if(x->x_addrinfo) {
    freeaddrinfo(x->x_addrinfo);
  }
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
  hints.ai_flags = AI_PASSIVE;    /* For wildcard IP address */
  hints.ai_protocol = 0;          /* Any protocol */
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;

  if(getaddrinfo(s->s_name, port, &hints, &x->x_addrinfo) != 0 ) {
    pd_error(x, "couldn't get address info for '%s:%s'", s->s_name, port);
  }
}


static void udpsocket_do_bind(t_udpsocket*x, t_symbol*ifaddr, unsigned short portno)
{
  static t_atom ap[1];
  struct sockaddr_in server;
  socklen_t serversize = sizeof(server);
  int sockfd = x->x_fd;
  int broadcast = 1;/* nonzero is true */
  memset(&server, 0, sizeof(server));

  SETFLOAT(ap, -1);
  if(x->x_port == portno && x->x_ifaddr == ifaddr) {
    return;
  }

  /* cleanup any open ports */
  if(sockfd >= 0) {
    //sys_rmpollfn(sockfd);
    iemnet__closesocket(sockfd, 0);
    x->x_fd = -1;
    x->x_port = -1;
    x->x_ifaddr = 0;
  }


  server.sin_family = AF_INET;

  if (ifaddr) {
    struct hostent *hp = gethostbyname(ifaddr->s_name);
    if(!hp) {
      iemnet_log(x, IEMNET_ERROR, "bad host '%s'?", ifaddr->s_name);
      iemnet__closesocket(sockfd, 0);
      return;
    }
    server.sin_family = hp->h_addrtype;
    memcpy((char *)&server.sin_addr, (char *)hp->h_addr, hp->h_length);
  } else {
    server.sin_addr.s_addr = INADDR_ANY;
  }

  sockfd = socket(server.sin_family, SOCK_DGRAM, 0);
  if(sockfd<0) {
    iemnet_log(x, IEMNET_ERROR, "unable to create socket");
    sys_sockerror("socket");
    return;
  }

  /* Enable sending of broadcast messages (if hostname is a broadcast address) */
#ifdef SO_BROADCAST
  if( 0 != setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST,
                      (const void *)&broadcast, sizeof(broadcast))) {
    iemnet_log(x, IEMNET_ERROR, "unable to switch to broadcast mode");
    sys_sockerror("setsockopt");
  }
#endif /* SO_BROADCAST */

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
                                        udpsocket_receive_callback,
                                        0);
  x->x_sender = iemnet__sender_create(sockfd, NULL, NULL, 0);

  x->x_fd = sockfd;
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

static int udpsocket_parse_bind(int argc, t_atom*argv, t_symbol**iface, unsigned short*port) {
  t_float f=0;
  switch(argc) {
  default:
    return 0;
  case 2:
    if(A_FLOAT == argv[0].a_type && A_SYMBOL == argv[1].a_type) {
      f = atom_getfloat(argv+0);
      if(f<0 || f>=0xFFFF)
        return 0;
      *port = (unsigned short)f;
      *iface=atom_getsymbol(argv+1);
      return 1;
    }
    return 0;
  case 1:
    switch(argv[0].a_type) {
    case A_FLOAT:
      f = atom_getfloat(argv+0);
      if(f<0 || f>=0xFFFF)
        return 0;
      *port = (unsigned short)f;
      *iface = 0;
      break;
    case A_SYMBOL:
      *iface = atom_getsymbol(argv+1);
      *port = 0;
      break;
    default:
      return 0;
    }

  }
  return 1;

}
static void udpsocket_bind(t_udpsocket*x, t_symbol*s, int argc, t_atom*argv) {
  t_symbol*iface;
  unsigned short port;
  (void)s;
  if(udpsocket_parse_bind(argc, argv, &iface, &port)) {
    udpsocket_do_bind(x, iface, port);
  } else {
    pd_error(x, "usage: bind [<uint16:port>] [<symbol:iface>]");
  }
}

/* constructor/destructor */

static void *udpsocket_new(t_symbol*s, int argc, t_atom*argv)
{
  t_symbol*iface=0;
  unsigned short port=0;
  t_udpsocket *x;
  (void)s;
  if(argc) {
    if(!udpsocket_parse_bind(argc, argv, &iface, &port)) {
      error("usage: udpsocket [<uint16:port>] [<symbol:iface>]");
      return 0;
    }
  }

  x = (t_udpsocket *)pd_new(udpsocket_class);
  x->x_msgout = outlet_new(&x->x_obj, 0); /* received data */
  x->x_statusout = outlet_new(&x->x_obj, 0); /* info */

  x->x_fd = -1;
  x->x_port = 0;
  x->x_ifaddr = 0;

  x->x_sender = NULL;
  x->x_receiver = NULL;

  x->x_floatlist = iemnet__floatlist_create(1024);
  udpsocket_do_bind(x, iface, port);
  return (x);
}

static void udpsocket_free(t_udpsocket *x)
{
  udpsocket_do_disconnect(x);
  if(x->x_floatlist) {
    iemnet__floatlist_destroy(x->x_floatlist);
  }
  x->x_floatlist = NULL;
  if(x->x_addrinfo) {
    freeaddrinfo(x->x_addrinfo);
  }
  x->x_addrinfo = NULL;
}

IEMNET_EXTERN void udpsocket_setup(void)
{
  if(!iemnet__register(objName)) {
    return;
  }
  udpsocket_class = class_new(gensym(objName), (t_newmethod)udpsocket_new,
                              (t_method)udpsocket_free,
                              sizeof(t_udpsocket), 0, A_GIMME, 0);
  class_addmethod(udpsocket_class, (t_method)udpsocket_bind,
                  gensym("bind"), A_GIMME, 0);
  class_addmethod(udpsocket_class, (t_method)udpsocket_connect, gensym("to"),
                  A_SYMBOL, A_FLOAT, 0);
  class_addmethod(udpsocket_class, (t_method)udpsocket_send, gensym("send"),
                  A_GIMME, 0);
  class_addlist(udpsocket_class, (t_method)udpsocket_send);
  class_addbang(udpsocket_class, (t_method)udpsocket_info);

  DEBUGMETHOD(udpsocket_class);
}


IEMNET_INITIALIZER(udpsocket_setup);

/* end of udpsocket.c */
