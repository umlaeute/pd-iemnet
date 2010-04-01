/* udpreceive.c
 * copyright (c) 2010 IOhannes m zmölnig, IEM
 * copyright (c) 2006-2010 Martin Peach
 * copyright (c) Miller Puckette
 */

/*                                                                              */
/* A server for unidirectional communication from within Pd.                     */
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
/* along with this program; if not, write to the Free Software                  */
/* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.  */
/*                                                                              */

static const char objName[] = "udpreceive";

#include "iemnet.h"

/* ----------------------------- udpreceive ------------------------- */

static t_class *udpreceive_class;

typedef struct _udpreceive
{
  t_object  x_obj;
  t_outlet  *x_msgout;
  t_outlet  *x_addrout;
  int       x_connectsocket;
  t_iemnet_receiver*x_receiver;
} t_udpreceive;


static void udpreceive_read_callback(void*y,
				     t_iemnet_chunk*c, 
				     int argc, t_atom*argv) {
  t_udpreceive*x=(t_udpreceive*)y;
  if(argc) {
    iemnet__addrout(NULL, x->x_addrout, c->addr, c->port);
    outlet_list(x->x_msgout, gensym("list"), argc, argv);
  } else {
    post("[%s] nothing received", objName);
  }
}

static void *udpreceive_new(t_floatarg fportno)
{
    t_udpreceive       *x;
    struct sockaddr_in server;
    int                sockfd, portno = fportno;
    int                intarg;

    /* create a socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    DEBUG("socket %d port %d", sockfd, portno);
    if (sockfd < 0)
    {
        sys_sockerror("udpreceive: socket");
        return (0);
    }
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;

    /* enable delivery of all multicast or broadcast (but not unicast)
    * UDP datagrams to all sockets bound to the same port */
    intarg = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
        (char *)&intarg, sizeof(intarg)) < 0)
      error("[%s] setsockopt (SO_REUSEADDR) failed", objName);

    /* assign server port number */
    server.sin_port = htons((u_short)portno);

    /* name the socket */
    if (bind(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        sys_sockerror("udpreceive: bind");
        sys_closesocket(sockfd);
        return (0);
    }

    x = (t_udpreceive *)pd_new(udpreceive_class);
    x->x_msgout = outlet_new(&x->x_obj, 0);
    x->x_addrout = outlet_new(&x->x_obj, gensym("list"));
    x->x_connectsocket = sockfd;

    //    sys_addpollfn(x->x_connectsocket, (t_fdpollfn)udpreceive_read, x);

    x->x_receiver=iemnet__receiver_create(sockfd,
					  x, 
					  udpreceive_read_callback);

    return (x);
}

static void udpreceive_free(t_udpreceive *x)
{
  iemnet__receiver_destroy(x->x_receiver);
  x->x_connectsocket=0;
}

IEMNET_EXTERN void udpreceive_setup(void)
{
  if(!iemnet__register(objName))return;
    udpreceive_class = class_new(gensym(objName),
        (t_newmethod)udpreceive_new, (t_method)udpreceive_free,
        sizeof(t_udpreceive), CLASS_NOINLET, A_DEFFLOAT, 0);
}

IEMNET_INITIALIZER(udpreceive_setup);

/* end udpreceive.c */
