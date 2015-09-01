/* tcpclient.c
 * copyright (c) 2010 IOhannes m zmölnig, IEM
 * copyright (c) 2006-2010 Martin Peach
 * copyright (c) 2004 Olaf Matthes
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

static t_class *tcpclient_class;
static char objName[] = "tcpclient";


typedef struct _tcpclient {
  t_object        x_obj;
  t_clock         *x_clock;
  t_outlet        *x_msgout;
  t_outlet        *x_addrout;
  t_outlet        *x_connectout;
  t_outlet        *x_statusout;

  t_iemnet_sender  *x_sender;
  t_iemnet_receiver*x_receiver;

  int              x_serialize;

  int             x_fd; // the socket
  char           *x_hostname; // address we want to connect to as text
  int             x_connectstate; // 0 = not connected, 1 = connected
  int             x_port; // port we're connected to
  long            x_addr; // address we're connected to as 32bit int


  t_iemnet_floatlist         *x_floatlist;
} t_tcpclient;


static void tcpclient_receive_callback(void *x, t_iemnet_chunk*);

static void tcpclient_info(t_tcpclient *x)
{
  // "server <socket> <IP> <port>"
  // "bufsize <insize> <outsize>"
  static t_atom output_atom[3];
  if(x&&x->x_connectstate) {
    int sockfd = x->x_fd;
    unsigned short port   = x->x_port;
    const char*hostname=x->x_hostname;

    int insize =iemnet__receiver_getsize(x->x_receiver);
    int outsize=iemnet__sender_getsize  (x->x_sender  );

    SETFLOAT (output_atom+0, sockfd);
    SETSYMBOL(output_atom+1, gensym(hostname));
    SETFLOAT (output_atom+2, port);

    outlet_anything( x->x_statusout, gensym("server"), 3, output_atom);

    SETFLOAT (output_atom+0, insize);
    SETFLOAT (output_atom+1, outsize);
    outlet_anything( x->x_statusout, gensym("bufsize"), 2, output_atom);
  }
}

/* connection handling */
static int tcpclient_do_disconnect(int fd, t_iemnet_sender*sender,
                                   t_iemnet_receiver*receiver)
{
  if(sender) {
    iemnet__sender_destroy(sender, 0);
    sender=NULL;
  }
  if(receiver) {
    iemnet__receiver_destroy(receiver, 0);
    receiver=NULL;
  }
  if (fd >= 0) {
    iemnet__closesocket(fd);
    return 1;
  }
  return 0;
}
static int tcpclient_do_connect(const char*host, unsigned short port,
                                t_tcpclient*x,
                                t_iemnet_sender**senderOUT, t_iemnet_receiver**receiverOUT, long*addrOUT)
{
  struct sockaddr_in  server;
  struct hostent      *hp;
  int                 sockfd=-1;
  t_iemnet_sender*  sender;
  t_iemnet_receiver*receiver;

  /* connect socket using hostname provided in command line */
  memset(&server, 0, sizeof(server));
  server.sin_family = AF_INET;
  hp = gethostbyname(host);
  if (hp == 0) {
    iemnet_log(x, IEMNET_ERROR, "bad host '%s'?", host);
    return (-1);
  }
  memcpy((char *)&server.sin_addr, (char *)hp->h_addr, hp->h_length);

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    iemnet_log(x, IEMNET_ERROR, "unable to open socket");
    sys_sockerror("socket");
    return (sockfd);
  }

  /* assign client port number */
  server.sin_port = htons((u_short)port);

  /* try to connect */
  if (connect(sockfd, (struct sockaddr *) &server, sizeof (server)) < 0) {
    iemnet_log(x, IEMNET_ERROR, "unable to connect to stream socket");
    sys_sockerror("connect");
    iemnet__closesocket(sockfd);
    return (-1);
  }

  sender=iemnet__sender_create(sockfd, NULL, NULL, 0);
  receiver=iemnet__receiver_create(sockfd, x,  tcpclient_receive_callback,
                                   0);

  if(addrOUT) {
    *addrOUT= ntohl(*(long *)hp->h_addr);
  }
  if(senderOUT) {
    *senderOUT=sender;
  }
  if(receiverOUT) {
    *receiverOUT=receiver;
  }
  return sockfd;
}

static void tcpclient_tick(t_tcpclient *x)
{
  outlet_float(x->x_connectout, 1);
}


static void tcpclient_disconnect(t_tcpclient *x);

static void tcpclient_connect(t_tcpclient *x, t_symbol *hostname,
                              t_floatarg fportno)
{
  long addr=0;
  int state;

  // first disconnect any active connection
  if(x->x_hostname || x->x_port) {
    state=tcpclient_do_disconnect(x->x_fd, x->x_sender, x->x_receiver);
    if(state) {
      outlet_float(x->x_connectout, 0);
    }
  }

  /* we get hostname and port and pass them on
     to the child thread that establishes the connection */
  x->x_hostname = hostname->s_name;
  x->x_port = fportno;


  state=tcpclient_do_connect(x->x_hostname, x->x_port, x,
                             &x->x_sender, &x->x_receiver,
                             &x->x_addr);
  x->x_connectstate=(state>0);
  x->x_fd=state;
  if(state>0) {
    outlet_float(x->x_connectout, 1);
  }
}

static void tcpclient_disconnect(t_tcpclient *x)
{
  if(x->x_hostname || x->x_port) {
    int state=tcpclient_do_disconnect(x->x_fd, x->x_sender, x->x_receiver);

    if(!state && !x->x_port) {
      iemnet_log(x, IEMNET_ERROR, "not connected");
    }
  }
  outlet_float(x->x_connectout, 0);

  x->x_port=0;
  x->x_hostname=NULL;
}

/* sending/receiving */

static void tcpclient_send(t_tcpclient *x, t_symbol *s, int argc,
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
  if(size<0) {
    tcpclient_disconnect(x);
  }
}

static void tcpclient_receive_callback(void*y, t_iemnet_chunk*c)
{
  t_tcpclient *x=(t_tcpclient*)y;

  if(c) {
    iemnet__addrout(x->x_statusout, x->x_addrout, x->x_addr, x->x_port);
    x->x_floatlist=iemnet__chunk2list(c,
                                      x->x_floatlist); // get's destroyed in the dtor
    iemnet__streamout(x->x_msgout, x->x_floatlist->argc, x->x_floatlist->argv,
                      x->x_serialize);
  } else {
    // disconnected
    tcpclient_disconnect(x);
  }
}

static void tcpclient_serialize(t_tcpclient *x, t_floatarg doit)
{
  x->x_serialize=doit;
}


/* constructor/destructor */
static void tcpclient_free_simple(t_tcpclient *x)
{
  if(x->x_clock) {
    clock_free(x->x_clock);
  }
  x->x_clock=NULL;
  if(x->x_floatlist) {
    iemnet__floatlist_destroy(x->x_floatlist);
  }
  x->x_floatlist=NULL;

  if(x->x_msgout) {
    outlet_free(x->x_msgout);
  }
  if(x->x_addrout) {
    outlet_free(x->x_addrout);
  }
  if(x->x_connectout) {
    outlet_free(x->x_connectout);
  }
  if(x->x_statusout) {
    outlet_free(x->x_statusout);
  }
}

static void *tcpclient_new(void)
{
  t_tcpclient *x = (t_tcpclient *)pd_new(tcpclient_class);
  x->x_msgout = outlet_new(&x->x_obj, 0);	/* received data */
  x->x_addrout = outlet_new(&x->x_obj, gensym("list"));
  x->x_connectout = outlet_new(&x->x_obj,
                               gensym("float"));	/* connection state */
  x->x_statusout = outlet_new(&x->x_obj,
                              0);/* last outlet for everything else */

  x->x_serialize=1;

  x->x_fd = -1;

  x->x_addr = 0L;
  x->x_port = 0;

  x->x_sender=NULL;
  x->x_receiver=NULL;

  x->x_clock = clock_new(x, (t_method)tcpclient_tick);
  x->x_floatlist=iemnet__floatlist_create(1024);

  return (x);
}

static void tcpclient_free(t_tcpclient *x)
{
  tcpclient_disconnect(x);
  tcpclient_free_simple(x);
}

IEMNET_EXTERN void tcpclient_setup(void)
{
  if(!iemnet__register(objName)) {
    return;
  }
  tcpclient_class = class_new(gensym(objName), (t_newmethod)tcpclient_new,
                              (t_method)tcpclient_free,
                              sizeof(t_tcpclient), 0, A_DEFFLOAT, 0);
  class_addmethod(tcpclient_class, (t_method)tcpclient_connect,
                  gensym("connect")
                  , A_SYMBOL, A_FLOAT, 0);
  class_addmethod(tcpclient_class, (t_method)tcpclient_disconnect,
                  gensym("disconnect"), 0);

  class_addmethod(tcpclient_class, (t_method)tcpclient_serialize,
                  gensym("serialize"), A_FLOAT, 0);

  class_addmethod(tcpclient_class, (t_method)tcpclient_send, gensym("send"),
                  A_GIMME, 0);
  class_addlist(tcpclient_class, (t_method)tcpclient_send);

  class_addbang(tcpclient_class, (t_method)tcpclient_info);
  DEBUGMETHOD(tcpclient_class);
}


IEMNET_INITIALIZER(tcpclient_setup);

/* end of tcpclient.c */
