/* tcpserver.c
 * copyright (c) 2010 IOhannes m zmölnig, IEM
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
/* along with this program; if not, write to the Free Software                  */
/* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.  */
/*                                                                              */

/* ---------------------------------------------------------------------------- */
//#define DEBUG
#include "iemnet.h"

#include "s_stuff.h"

#if defined(UNIX) || defined(unix)
# include <arpa/inet.h>
#else
# include <winsock2.h>
#endif


#define MAX_CONNECT 32 /* maximum number of connections */

/* ----------------------------- tcpserver ------------------------- */

static t_class *tcpserver_class;
static char objName[] = "tcpserver";

typedef struct _tcpserver_socketreceiver
{
  t_symbol                    *sr_host;
  t_int                       sr_fd;
  t_iemnet_sender*sr_sender;
  t_iemnet_receiver*sr_receiver;
} t_tcpserver_socketreceiver;

typedef struct _tcpserver
{
  t_object                    x_obj;
  t_outlet                    *x_msgout;
  t_outlet                    *x_connectout;
  t_outlet                    *x_status_outlet;

  t_tcpserver_socketreceiver  *x_sr[MAX_CONNECT]; /* socket per connection */
  t_int                       x_nconnections;

  t_int                       x_connectsocket;    /* socket waiting for new connections */

  t_atom                      x_addrbytes[4];
} t_tcpserver;

static void tcpserver_receive_callback(t_tcpserver *x, int sockfd, int argc, t_atom*argv);

static t_tcpserver_socketreceiver *tcpserver_socketreceiver_new(void *owner, int sockfd, t_symbol*host)
{
  t_tcpserver_socketreceiver *x = (t_tcpserver_socketreceiver *)getbytes(sizeof(*x));
  if(NULL==x) {
    error("%s_socketreceiver: unable to allocate %d bytes", objName, sizeof(*x));
    return NULL;
  } else {
      x->sr_host=host;
      x->sr_fd=sockfd;

      x->sr_sender=iemnet__sender_create(sockfd);
      x->sr_receiver=iemnet__receiver_create(sockfd, owner, (t_iemnet_receivecallback)tcpserver_receive_callback);
  }
  return (x);
}

static void tcpserver_socketreceiver_free(t_tcpserver_socketreceiver *x)
{
  if (x != NULL)
    {
      if(x->sr_sender)  iemnet__sender_destroy(x->sr_sender);
      if(x->sr_receiver)iemnet__receiver_destroy(x->sr_receiver);

      sys_closesocket(x->sr_fd);

      freebytes(x, sizeof(*x));
    }
}

static int tcpserver_socket2index(t_tcpserver*x, int sockfd)
{
  int i=0;
  for(i = 0; i < x->x_nconnections; i++) /* check if connection exists */
    {
      if(x->x_sr[i]->sr_fd == sockfd)
        {
          return i;
        }
    }  
  return -1;
}

/* checks whether client is a valid (1-based) index
 *  if the id is invalid, returns -1
 *  if the id is valid, return the 0-based index (client-1)
 */
static int tcpserver_fixindex(t_tcpserver*x, int client)
{
  if(x->x_nconnections <= 0)
    {
      pd_error(x, "[%s]: no clients connected", objName);
      return -1;
    }
  
  if (!((client > 0) && (client <= x->x_nconnections)))
    {
      pd_error(x, "[%s] client %d out of range [1..%d]", objName, client, x->x_nconnections);
      return -1;
    }
  return (client-1);
}

/* ---------------- main tcpserver (send) stuff --------------------- */

static void tcpserver_send_bytes(t_tcpserver*x, int client, t_iemnet_chunk*chunk)
{
  if(x && x->x_sr && x->x_sr[client]) {
    t_atom                  output_atom[3];
    int size=0;

    t_iemnet_sender*sender=sender=x->x_sr[client]->sr_sender;
    int sockfd = x->x_sr[client]->sr_fd;

    if(sender) {
      size=iemnet__sender_send(sender, chunk);
    }

    SETFLOAT(&output_atom[0], client+1);
    SETFLOAT(&output_atom[1], size);
    SETFLOAT(&output_atom[2], sockfd);
    outlet_anything( x->x_status_outlet, gensym("sent"), 3, output_atom);
  }
}

/* send message to client using client number
   note that the client numbers might change in case a client disconnects! */
/* clients start at 1 but our index starts at 0 */
static void tcpserver_send_client(t_tcpserver *x, t_symbol *s, int argc, t_atom *argv)
{
  if (argc > 1)
    {
      t_iemnet_chunk*chunk=NULL;
      int client=tcpserver_fixindex(x, atom_getint(argv));
      if(client<0)return;
      chunk=iemnet__chunk_create_list(argc-1, argv+1);
      --client;/* zero based index*/
      tcpserver_send_bytes(x, client, chunk);
      return;
    }
  else 
    {
      pd_error(x, "[%s] no client specified", objName);
    }
}

/* broadcasts a message to all connected clients */
static void tcpserver_broadcast(t_tcpserver *x, t_symbol *s, int argc, t_atom *argv)
{
  int     client;
  t_iemnet_chunk*chunk=iemnet__chunk_create_list(argc, argv);

  /* enumerate through the clients and send each the message */
  for(client = 0; client < x->x_nconnections; client++)	/* check if connection exists */
    {
      /* socket exists for this client */
      tcpserver_send_bytes(x, client, chunk);
    }
  iemnet__chunk_destroy(chunk);
}

/* broadcasts a message to all connected clients */
static void tcpserver_broadcastbut(t_tcpserver *x, t_symbol *s, int argc, t_atom *argv)
{
  int client=0;
  int but=-1;
  t_iemnet_chunk*chunk=NULL;

  if(argc<2) {
    return;
  }
  if((but=tcpserver_fixindex(x, atom_getint(argv)))<0)return;

  chunk=iemnet__chunk_create_list(argc+1, argv+1);

  /* enumerate through the clients and send each the message */
  for(client = 0; client < x->x_nconnections; client++)	/* check if connection exists */
    {
      /* socket exists for this client */
      if(client!=but)tcpserver_send_bytes(x, client, chunk);
    }
  iemnet__chunk_destroy(chunk);
}

/* send message to client using socket number */
static void tcpserver_send_socket(t_tcpserver *x, t_symbol *s, int argc, t_atom *argv)
{
  int     client = -1;
  t_iemnet_chunk*chunk=NULL;
  if(argc) {
    client = tcpserver_socket2index(x, atom_getint(argv));
    if(client<0)return;
  } else {
    pd_error(x, "%s_send: no socket specified", objName);
    return;
  }

  /* get socket number of connection (first element in list) */
  if(argc && argv->a_type == A_FLOAT)
    {
      int sockfd=atom_getint(argv);
      client = tcpserver_socket2index(x, sockfd);
      if(client < 0)
        {
          post("%s_send: no connection on socket %d", objName, sockfd);
          return;
        }
    }
  else
    {
      post("%s_send: no socket specified", objName);
      return;
    }
  
  chunk=iemnet__chunk_create_list(argc-1, argv+1);
  tcpserver_send_bytes(x, client, chunk);
  iemnet__chunk_destroy(chunk);
}

static void tcpserver_disconnect(t_tcpserver *x, int client)
{
  int k;

  tcpserver_socketreceiver_free(x->x_sr[client]);
  x->x_sr[client]=NULL;

  /* rearrange list now: move entries to close the gap */
  for(k = client; k < x->x_nconnections; k++)
    {
      x->x_sr[k] = x->x_sr[k + 1];
    }
  x->x_sr[k + 1]=NULL;
  x->x_nconnections--;

  outlet_float(x->x_connectout, x->x_nconnections);
}


/* disconnect a client by number */
static void tcpserver_disconnect_client(t_tcpserver *x, t_floatarg fclient)
{
  int client = tcpserver_fixindex(x, fclient);

  if(client<0)return;
  tcpserver_disconnect(x, client);
}


/* disconnect a client by socket */
static void tcpserver_disconnect_socket(t_tcpserver *x, t_floatarg fsocket)
{
  int id=tcpserver_socket2index(x, (int)fsocket);
  if(id>=0)
    tcpserver_disconnect_client(x, id+1);
}



/* disconnect a client by socket */
static void tcpserver_disconnect_all(t_tcpserver *x)
{
  int id=x->x_nconnections;
  while(--id>=0) {
    tcpserver_disconnect(x, id);
  }
}





/* ---------------- main tcpserver (receive) stuff --------------------- */
static void tcpserver_receive_callback(t_tcpserver *x, int sockfd, int argc, t_atom*argv) {
   if(argc) {
    outlet_list(x->x_msgout, gensym("list"), argc, argv);
  } else {
    // disconnected
    tcpserver_disconnect_socket(x, sockfd);
  }

  //  post("tcpserver: %d bytes in %d packets", bytecount, packetcount);
}

static void tcpserver_connectpoll(t_tcpserver *x)
{
  struct sockaddr_in  incomer_address;
  unsigned int        sockaddrl = sizeof( struct sockaddr );
  int                 fd = accept(x->x_connectsocket, (struct sockaddr*)&incomer_address, &sockaddrl);
  int                 i;

  if (fd < 0) post("%s: accept failed", objName);
  else
    {
      t_symbol*host=gensym(inet_ntoa(incomer_address.sin_addr));
      t_tcpserver_socketreceiver *y = tcpserver_socketreceiver_new((void *)x, fd, host);
      if (!y)
        {
          sys_closesocket(fd);
          return;
        }
      x->x_nconnections++;
      i = x->x_nconnections - 1;
      x->x_sr[i] = y;
    }

  outlet_float(x->x_connectout, x->x_nconnections);
}

static void *tcpserver_new(t_floatarg fportno)
{
  t_tcpserver         *x;
  int                 i;
  struct sockaddr_in  server;
  int                 sockfd, portno = fportno;

  /* create a socket */
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
#ifdef DEBUG
  post("%s: receive socket %d", objName, sockfd);
#endif
  if (sockfd < 0)
    {
      sys_sockerror("tcpserver: socket");
      return (0);
    }
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;

  /* assign server port number */
  server.sin_port = htons((u_short)portno);
  /* name the socket */
  if (bind(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
      sys_sockerror("tcpserver: bind");
      sys_closesocket(sockfd);
      return (0);
    }

  x = (t_tcpserver *)pd_new(tcpserver_class);
  x->x_msgout = outlet_new(&x->x_obj, 0); /* 1st outlet for received data */



  /* streaming protocol */
  if (listen(sockfd, 5) < 0)
    {
      sys_sockerror("tcpserver: listen");
      sys_closesocket(sockfd);
      sockfd = -1;
    }
  else
    {
      sys_addpollfn(sockfd, (t_fdpollfn)tcpserver_connectpoll, x); // wait for new connections 
      x->x_connectout = outlet_new(&x->x_obj, gensym("float")); /* 2nd outlet for number of connected clients */
      x->x_status_outlet = outlet_new(&x->x_obj, 0);/* 5th outlet for everything else */
    }
  x->x_connectsocket = sockfd;
  x->x_nconnections = 0;

  for(i = 0; i < MAX_CONNECT; i++)
    {
      x->x_sr[i] = NULL;
    }
  /* prepare to convert the bytes in the buffer to floats in a list */
  for (i = 0; i < 4; ++i)
    {
      x->x_addrbytes[i].a_type = A_FLOAT;
      x->x_addrbytes[i].a_w.w_float = 0;
    }
  return (x);
}

static void tcpserver_free(t_tcpserver *x)
{
  int     i;

  for(i = 0; i < MAX_CONNECT; i++)
    {
      if (NULL!=x->x_sr[i]) {
        tcpserver_socketreceiver_free(x->x_sr[i]);
      }
    }
  if (x->x_connectsocket >= 0)
    {
      sys_rmpollfn(x->x_connectsocket);
      sys_closesocket(x->x_connectsocket);
    }
}

IEMNET_EXTERN void  tcpserver_setup(void)
{
  static int again=0; if(again)return; again=1;

  tcpserver_class = class_new(gensym(objName),(t_newmethod)tcpserver_new, (t_method)tcpserver_free,
                              sizeof(t_tcpserver), 0, A_DEFFLOAT, 0);
  class_addmethod(tcpserver_class, (t_method)tcpserver_disconnect_client, gensym("disconnectclient"), A_DEFFLOAT, 0);
  class_addmethod(tcpserver_class, (t_method)tcpserver_disconnect_socket, gensym("disconnectsocket"), A_DEFFLOAT, 0);
  class_addmethod(tcpserver_class, (t_method)tcpserver_disconnect_all, gensym("disconnect"), 0);

  class_addmethod(tcpserver_class, (t_method)tcpserver_send_socket, gensym("send"), A_GIMME, 0);
  class_addmethod(tcpserver_class, (t_method)tcpserver_send_client, gensym("client"), A_GIMME, 0);

  class_addmethod(tcpserver_class, (t_method)tcpserver_broadcast, gensym("broadcast"), A_GIMME, 0);

  class_addmethod(tcpserver_class, (t_method)tcpserver_broadcastbut, gensym("broadcastbut"), A_GIMME, 0);

  class_addlist(tcpserver_class, (t_method)tcpserver_broadcast);

  post("iemnet: networking with Pd :: %s", objName);
  post("        (c) 2010 IOhannes m zmoelnig, IEM");
  post("        based on mrpeach/net, based on maxlib");
}

IEMNET_INITIALIZER(tcpserver_setup);


/* end of tcpserver.c */
