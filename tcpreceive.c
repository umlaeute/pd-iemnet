/* tcpreceive.c
 * copyright (c) 2010 IOhannes m zmölnig, IEM
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
/* along with this program; if not, write to the Free Software                  */
/* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.  */
/*                                                                              */


#include "iemnet.h"
#ifndef _WIN32
# include <netinet/tcp.h>
#endif

/* ----------------------------- tcpreceive ------------------------- */

static t_class *tcpreceive_class;

#define MAX_CONNECTIONS 128 // this is going to cause trouble down the line...:(

typedef struct _tcpconnection
{
  long            addr;
  unsigned short  port;
  int             socket;
  struct _tcpreceive*owner;
  t_iemnet_receiver*receiver;
} t_tcpconnection;

typedef struct _tcpreceive
{
  t_object        x_obj;
  t_outlet        *x_msgout;
  t_outlet        *x_addrout;
  t_outlet        *x_connectout;

  int             x_connectsocket;

  int             x_nconnections;
  t_tcpconnection x_connection[MAX_CONNECTIONS];

  t_atom          x_addrbytes[5];
} t_tcpreceive;


static int tcpreceive_find_socket(t_tcpreceive *x, int fd) {
  int i;
  for (i = 0; i < MAX_CONNECTIONS; ++i)
    if (x->x_connection[i].socket == fd)return i;

  return -1;
}

static int tcpreceive_disconnect(t_tcpreceive *x, int id);

static void tcpreceive_read_callback(t_tcpconnection *y, int argc, t_atom*argv)
{
  t_tcpreceive*x=NULL;
  if(NULL==y || NULL==(x=y->owner))return;
  int index=tcpreceive_find_socket(x, y->socket);
  if(index>=0) {

    if(argc) {
      // outlet info about connection
      outlet_list(x->x_msgout, gensym("list"), argc, argv);
    } else {
      // disconnected
      int sockfd=y->socket;
      tcpreceive_disconnect(x, index);
    }
  }

}


/* tcpreceive_addconnection tries to add the socket fd to the list */
/* returns 1 on success, else 0 */
static int tcpreceive_addconnection(t_tcpreceive *x, int fd, long addr, unsigned short port)
{
  int i;
  for (i = 0; i < MAX_CONNECTIONS; ++i)
    {
      if (x->x_connection[i].socket == -1)
        {
	  x->x_connection[i].socket = fd;
	  x->x_connection[i].addr = addr;
	  x->x_connection[i].port = port;
	  x->x_connection[i].owner = x;
	  x->x_connection[i].receiver=
	    iemnet__receiver_create(fd, 
				    x->x_connection+i, 
				    (t_iemnet_receivecallback)tcpreceive_read_callback);
	  
	  return 1;
        }
    }
  return 0;
}


/* tcpreceive_connectpoll checks for incoming connection requests on the original socket */
/* a new socket is assigned  */
static void tcpreceive_connectpoll(t_tcpreceive *x)
{
  struct sockaddr_in  from;
  socklen_t           fromlen = sizeof(from);
  long                addr;
  unsigned short      port;
  int                 fd;

  fd = accept(x->x_connectsocket, (struct sockaddr *)&from, &fromlen);
  if (fd < 0) post("tcpreceive: accept failed");
  else
    {
      //       t_socketreceiver *y = socketreceiver_new((void *)x,
      //         (t_socketnotifier)tcpreceive_notify,
      //           0, 0);
      
      /* get the sender's ip */
      addr = ntohl(from.sin_addr.s_addr);
      port = ntohs(from.sin_port);
      if (tcpreceive_addconnection(x, fd, addr, port))
	{
	  outlet_float(x->x_connectout, ++x->x_nconnections);
	  x->x_addrbytes[0].a_w.w_float = (addr & 0xFF000000)>>24;
	  x->x_addrbytes[1].a_w.w_float = (addr & 0x0FF0000)>>16;
	  x->x_addrbytes[2].a_w.w_float = (addr & 0x0FF00)>>8;
	  x->x_addrbytes[3].a_w.w_float = (addr & 0x0FF);
	  x->x_addrbytes[4].a_w.w_float = port;
	  outlet_list(x->x_addrout, &s_list, 5L, x->x_addrbytes);
        }
      else
        {
	  error ("tcpreceive: Too many connections");
	  sys_closesocket(fd);
        }
    }
}


static int tcpreceive_disconnect(t_tcpreceive *x, int id)
{
  if(id>=0 && id < MAX_CONNECTIONS && x->x_connection[id].port>0) {
    iemnet__receiver_destroy(x->x_connection[id].receiver);
    x->x_connection[id].receiver=NULL;

    sys_closesocket(x->x_connection[id].socket);
    x->x_connection[id].socket = -1;

    x->x_connection[id].addr = 0L;
    x->x_connection[id].port = 0;
    x->x_nconnections--;
    outlet_float(x->x_connectout, x->x_nconnections);
    return 1;
  }

  return 0;
}

/* tcpreceive_closeall closes all open sockets and deletes them from the list */
static void tcpreceive_disconnect_all(t_tcpreceive *x)
{
  int i;

  for (i = 0; i < MAX_CONNECTIONS; i++)
    {
      tcpreceive_disconnect(x, i);
    }
}




/* tcpreceive_removeconnection tries to delete the socket fd from the list */
/* returns 1 on success, else 0 */
static int tcpreceive_disconnect_socket(t_tcpreceive *x, int fd)
{
  int i;
  for (i = 0; i < MAX_CONNECTIONS; ++i)
    {
      if (x->x_connection[i].socket == fd)
        {
	  return tcpreceive_disconnect(x, i);
        }
    }
  return 0;
}

static void tcpreceive_free(t_tcpreceive *x)
{ /* is this ever called? */
  if (x->x_connectsocket >= 0)
    {
      sys_rmpollfn(x->x_connectsocket);
      sys_closesocket(x->x_connectsocket);
    }
  tcpreceive_disconnect_all(x);
}

static void *tcpreceive_new(t_floatarg fportno)
{
  t_tcpreceive       *x;
  struct sockaddr_in server;
  int                sockfd, portno = fportno;
  int                intarg, i;

  /* create a socket */
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  DEBUG("socket %d port %d", sockfd, portno);
  if (sockfd < 0)
    {
      sys_sockerror("tcpreceive: socket");
      return (0);
    }
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;

  /* ask OS to allow another Pd to repoen this port after we close it. */
  intarg = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
		 (char *)&intarg, sizeof(intarg)) < 0)
    post("tcpreceive: setsockopt (SO_REUSEADDR) failed");
  /* Stream (TCP) sockets are set NODELAY */
  intarg = 1;
  if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY,
		 (char *)&intarg, sizeof(intarg)) < 0)
    post("setsockopt (TCP_NODELAY) failed\n");

  /* assign server port number */
  server.sin_port = htons((u_short)portno);

  /* name the socket */
  if (bind(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
      sys_sockerror("tcpreceive: bind");
      sys_closesocket(sockfd);
      return (0);
    }
  x = (t_tcpreceive *)pd_new(tcpreceive_class);
  x->x_msgout = outlet_new(&x->x_obj, &s_anything);
  x->x_addrout = outlet_new(&x->x_obj, &s_list);
  x->x_connectout = outlet_new(&x->x_obj, &s_float);
  /* clear the connection list */
  for (i = 0; i < MAX_CONNECTIONS; ++i)
    {
      x->x_connection[i].socket = -1;
      x->x_connection[i].addr = 0L;
      x->x_connection[i].port = 0;
    }
  for (i = 0; i < 5; ++i)
    {
      SETFLOAT(x->x_addrbytes+i, 0);
    }

  /* streaming protocol */
  if (listen(sockfd, 5) < 0)
    {
      sys_sockerror("tcpreceive: listen");
      sys_closesocket(sockfd);
      sockfd = -1;
    }
  else
    {
      sys_addpollfn(sockfd, (t_fdpollfn)tcpreceive_connectpoll, x);
    }
  x->x_connectsocket = sockfd;
  x->x_nconnections = 0;

  return (x);
}


void tcpreceive_setup(void)
{
  tcpreceive_class = class_new(gensym("tcpreceive"),
			       (t_newmethod)tcpreceive_new, (t_method)tcpreceive_free,
			       sizeof(t_tcpreceive), CLASS_NOINLET, A_DEFFLOAT, 0);
}

/* end x_net_tcpreceive.c */

