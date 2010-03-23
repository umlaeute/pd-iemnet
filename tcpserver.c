/* tcpserver.c
 * copyright (c) 2010 IOhannes m zmölnig, IEM
 * copyright (c) 2006-2010 Martin Peach
 * copyright (c) 2004 Olaf Matthes
 */

/*                                                                              */
/* A server for bidirectional communication from within Pd.                     */
/* Allows to send back data to specific clients connected to the server.        */
/* Written by Olaf Matthes <olaf.matthes@gmx.de>                                */
/* Get source at http://www.akustische-kunst.org/puredata/maxlib                */
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

#include "m_pd.h"
#include "m_imp.h"
#include "s_stuff.h"

#include <sys/types.h>
#include <stdio.h>
#include <pthread.h>
#if defined(UNIX) || defined(unix)
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/ioctl.h> /* linux has the SIOCOUTQ ioctl */
#define SOCKET_ERROR -1
#else
#include <winsock2.h>
#endif


#ifdef _MSC_VER
#define snprintf sprintf_s
#endif

#define MAX_CONNECT 32 /* maximum number of connections */
#define INBUFSIZE 65536L /* was 4096: size of receiving data buffer */
#define MAX_UDP_RECEIVE 65536L /* longer than data in maximum UDP packet */



 /* draft:
  *   - there is a sender thread for each open connection
  *   - the main thread just adds chunks to each sender threads processing queue
  *   - the sender thread tries to send the queue as fast as possible
  */


 /* data handling */
#include <string.h>

typedef struct _chunk {
  unsigned char* data;
  size_t size;
} t_chunk;
void chunk_destroy(t_chunk*c) {
  if(NULL==c)return;

  if(c->data)freebytes(c->data, c->size*sizeof(unsigned char));

  c->data=NULL;
  c->size=0;

  freebytes(c, sizeof(t_chunk));
}

t_chunk* chunk_create(int argc, t_atom*argv) {
  t_chunk*result=(t_chunk*)getbytes(sizeof(t_chunk));
  int i;
  if(NULL==result)return NULL;

  result->size=argc;
  result->data=(unsigned char*)getbytes(sizeof(unsigned char)*argc);

  if(NULL == result->data) {
    result->size=0;
    chunk_destroy(result);
    return NULL;
  }

  for(i=0; i<argc; i++) {
    unsigned char c = atom_getint(argv);
    result->data[i]=c;
    argv++;
  }

  return result;
}

t_chunk*chunk_duplicate(t_chunk*c) {
  t_chunk*result=NULL;
  if(NULL==c)return NULL;

  result=(t_chunk*)getbytes(sizeof(t_chunk));

  result->size=c->size;
  result->data=(unsigned char*)getbytes(sizeof(unsigned char)*(result->size));
  if(NULL == result->data) {
    result->size=0;
    chunk_destroy(result);
    return NULL;
  }

  memcpy(result->data, c->data, result->size);

  return result;
}


/* queue handling */
typedef struct _node {
  struct _node* next;
  t_chunk*data;
} t_node;

typedef struct _queue {
  t_node* head; /* = 0 */
  t_node* tail; /* = 0 */
  pthread_mutex_t mtx;
  pthread_cond_t cond;

  int done; // in cleanup state
  int size;
} t_queue;


static int queue_push(
                      t_queue* const _this,
                      t_chunk* const data
                      ) {
  t_node* tail;
  t_node* n=NULL;
  int size=_this->size;

  if(NULL == data) return size;
  fprintf(stderr, "pushing %d bytes\n", data->size);

  n=(t_node*)getbytes(sizeof(t_node));

  n->next = 0;
  n->data = data;
  pthread_mutex_lock(&_this->mtx);
  if (! (tail = _this->tail)) {
    _this->head = n;
  } else {
    tail->next = n;
  }
  _this->tail = n;

  _this->size+=data->size;
  size=_this->size;


  fprintf(stderr, "pushed %d bytes\n", data->size);

  pthread_mutex_unlock(&_this->mtx);
  pthread_cond_signal(&_this->cond);

  return size;
}

t_chunk* queue_pop(
                   t_queue* const _this
                   ) {
  t_node* head=0;
  t_chunk*data=0;
  pthread_mutex_lock(&_this->mtx);
  while (! (head = _this->head)) {
    if(_this->done) {
      pthread_mutex_unlock(&_this->mtx);
      return NULL;
    }
    else
      pthread_cond_wait(&_this->cond, &_this->mtx);
  }
  if (! (_this->head = head->next)) {
    _this->tail = 0;
  }
  if(head && head->data) {
    _this->size-=head->data->size;
  }

  pthread_mutex_unlock(&_this->mtx);
  if(head) {
    data=head->data;
    freebytes(head, sizeof(t_node));
    head=NULL;
  }
  fprintf(stderr, "popped %d bytes\n", data->size);
  return data;
}

void queue_finish(t_queue* q) {
  if(NULL==q) 
    return;
  q->done=1;
  pthread_cond_signal(&q->cond);
}

void queue_destroy(t_queue* q) {
  t_chunk*c=NULL;
  if(NULL==q) 
    return;

  queue_finish(q);

  /* remove all the chunks from the queue */
  while(NULL!=(c=queue_pop(q))) {
    chunk_destroy(c);
  }

  q->head=NULL;
  q->tail=NULL;

  pthread_mutex_destroy(&q->mtx);
  pthread_cond_destroy(&q->cond);

  freebytes(q, sizeof(t_queue));
  q=NULL;
}

t_queue* queue_create(void) {
  static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
  static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

  t_queue*q=(t_queue*)getbytes(sizeof(t_queue));
  if(NULL==q)return NULL;

  q->head = NULL;
  q->tail = NULL;

  memcpy(&q->cond, &cond, sizeof(pthread_cond_t));
  memcpy(&q->mtx , &mtx, sizeof(pthread_mutex_t));

  q->done = 0;
  q->size = 0;

  return q;
}


typedef struct _sender {
  pthread_t thread;

  int sockfd; /* owned outside; must call sender_destroy() before freeing socket yourself */
  t_queue*queue;
  int cont; // indicates whether we want to thread to continue or to terminate
} t_sender;

/* the workhorse of the family */
static void*sender_sendthread(void*arg) {
  t_sender*sender=(t_sender*)arg;

  int sockfd=sender->sockfd;
  t_queue*q=sender->queue;

  while(sender->cont) {
    t_chunk*c=NULL;
    c=queue_pop(q);
    if(c) {
      unsigned char*data=c->data;
      unsigned int size=c->size;

      int result = send(sockfd, data, size, 0);
     

      // shouldn't we do something with the result here?

      chunk_destroy(c);
    }
  }
  sender->queue=NULL;
  queue_destroy(q);
  return NULL;
}

static int sender_send(t_sender*s, t_chunk*c) {
  t_queue*q=s->queue;
  int size=0;
  if(q) {
    size = queue_push(q, c);
  }
  return size;
}

static void sender_destroy(t_sender*s) {
  s->cont=0;
  if(s->queue)
    queue_finish(s->queue);

  s->sockfd = -1;

  pthread_join(s->thread, NULL);

  freebytes(s, sizeof(t_sender));
  s=NULL;
}
static t_sender*sender_create(int sock) {
  t_sender*result=(t_sender*)getbytes(sizeof(t_sender));
  int res=0;

  if(NULL==result)return NULL;

  result->queue = queue_create();
  result->sockfd = sock;
  result->cont =1;

  res=pthread_create(&result->thread, 0, sender_sendthread, result);

  if(0==res) {

  } else {
    // something went wrong
  }

  return result;
}

/* ----------------------------- tcpserver ------------------------- */

static t_class *tcpserver_class;
static char objName[] = "tcpserver";

typedef void (*t_tcpserver_socketnotifier)(void *x);
typedef void (*t_tcpserver_socketreceivefn)(void *x, t_binbuf *b);

typedef struct _tcpserver_socketreceiver
{
  t_symbol                    *sr_host;
  t_int                       sr_fd;
  t_int                       sr_fdbuf;
  u_long                      sr_addr;
  unsigned char               *sr_inbuf;
  int                         sr_inhead;
  int                         sr_intail;
  void                        *sr_owner;
  t_tcpserver_socketnotifier  sr_notifier;
  t_tcpserver_socketreceivefn sr_socketreceivefn;
  t_sender*sr_sender;
} t_tcpserver_socketreceiver;

typedef struct _tcpserver
{
  t_object                    x_obj;
  t_outlet                    *x_msgout;
  t_outlet                    *x_connectout;
  t_outlet                    *x_sockout;
  t_outlet                    *x_addrout;
  t_outlet                    *x_status_outlet;

  t_tcpserver_socketreceiver  *x_sr[MAX_CONNECT];

  t_int                       x_sock_fd;
  t_int                       x_connectsocket;
  t_int                       x_nconnections;

  t_atom                      x_addrbytes[4];
  t_atom                      x_msgoutbuf[MAX_UDP_RECEIVE];
} t_tcpserver;

static t_tcpserver_socketreceiver *tcpserver_socketreceiver_new(void *owner, t_tcpserver_socketnotifier notifier,
                                                                t_tcpserver_socketreceivefn socketreceivefn);
static int tcpserver_socketreceiver_doread(t_tcpserver_socketreceiver *x);
static void tcpserver_socketreceiver_read(t_tcpserver_socketreceiver *x, int fd);
static void tcpserver_socketreceiver_free(t_tcpserver_socketreceiver *x);
static void tcpserver_send(t_tcpserver *x, t_symbol *s, int argc, t_atom *argv);
static void tcpserver_send_bytes(int sockfd, t_tcpserver *x, int argc, t_atom *argv);
#ifdef SIOCOUTQ
static int tcpserver_send_buffer_avaliable_for_client(t_tcpserver *x, int client);
#endif
static void tcpserver_client_send(t_tcpserver *x, t_symbol *s, int argc, t_atom *argv);
static void tcpserver_output_client_state(t_tcpserver *x, int client);
static int  tcpserver_set_socket_send_buf_size(int sockfd, int size);
static int  tcpserver_get_socket_send_buf_size(int sockfd);
static void tcpserver_disconnect(t_tcpserver *x);
static void tcpserver_client_disconnect(t_tcpserver *x, t_floatarg fclient);
static void tcpserver_socket_disconnect(t_tcpserver *x, t_floatarg fsocket);
static void tcpserver_broadcast(t_tcpserver *x, t_symbol *s, int argc, t_atom *argv);
static void tcpserver_notify(t_tcpserver *x);
static void tcpserver_connectpoll(t_tcpserver *x);
static void tcpserver_print(t_tcpserver *x);
static void *tcpserver_new(t_floatarg fportno);
static void tcpserver_free(t_tcpserver *x);
void tcpserver_setup(void);

static t_tcpserver_socketreceiver *tcpserver_socketreceiver_new(void *owner, t_tcpserver_socketnotifier notifier,
                                                                t_tcpserver_socketreceivefn socketreceivefn)
{
  t_tcpserver_socketreceiver *x = (t_tcpserver_socketreceiver *)getbytes(sizeof(*x));
  if (!x)
    {
      error("%s_socketreceiver: unable to allocate %d bytes", objName, sizeof(*x));
    }
  else
    {
      x->sr_inhead = x->sr_intail = 0;
      x->sr_owner = owner;
      x->sr_notifier = notifier;
      x->sr_socketreceivefn = socketreceivefn;
      if (!(x->sr_inbuf = malloc(INBUFSIZE)))
        {
          freebytes(x, sizeof(*x));
          x = NULL;
          error("%s_socketreceiver: unable to allocate %ld bytes", objName, INBUFSIZE);
        }
    }
  return (x);
}

/* this is in a separately called subroutine so that the buffer isn't
   sitting on the stack while the messages are getting passed. */
static int tcpserver_socketreceiver_doread(t_tcpserver_socketreceiver *x)
{
  char            messbuf[INBUFSIZE];
  char            *bp = messbuf;
  int             indx, i;
  int             inhead = x->sr_inhead;
  int             intail = x->sr_intail;
  unsigned char   c;
  t_tcpserver     *y = x->sr_owner;
  unsigned char   *inbuf = x->sr_inbuf;

  if (intail == inhead) return (0);
#ifdef DEBUG
  post ("%s_socketreceiver_doread: intail=%d inhead=%d", objName, intail, inhead);
#endif

  for (indx = intail, i = 0; indx != inhead; indx = (indx+1)&(INBUFSIZE-1), ++i)
    {
      c = *bp++ = inbuf[indx];
      y->x_msgoutbuf[i].a_w.w_float = (float)c;
    }

  if (i > 1) outlet_list(y->x_msgout, &s_list, i, y->x_msgoutbuf);
  else outlet_float(y->x_msgout, y->x_msgoutbuf[0].a_w.w_float);

  //   intail = (indx+1)&(INBUFSIZE-1);
  x->sr_inhead = inhead;
  x->sr_intail = indx;//intail;
  return (1);
}

static void tcpserver_socketreceiver_read(t_tcpserver_socketreceiver *x, int fd)
{
  int         readto = (x->sr_inhead >= x->sr_intail ? INBUFSIZE : x->sr_intail-1);
  int         ret, i;
  t_tcpserver *y = x->sr_owner;

  y->x_sock_fd = fd;
  /* the input buffer might be full.  If so, drop the whole thing */
  if (readto == x->sr_inhead)
    {
      post("%s: dropped message", objName);
      x->sr_inhead = x->sr_intail = 0;
      readto = INBUFSIZE;
    }
  else
    {
      ret = recv(fd, x->sr_inbuf + x->sr_inhead,
                 readto - x->sr_inhead, 0);
      if (ret < 0)
        {
          sys_sockerror("tcpserver: recv");
          if (x->sr_notifier) (*x->sr_notifier)(x->sr_owner);
          sys_rmpollfn(fd);
          sys_closesocket(fd);
        }
      else if (ret == 0)
        {
          post("%s: connection closed on socket %d", objName, fd);
          if (x->sr_notifier) (*x->sr_notifier)(x->sr_owner);
          sys_rmpollfn(fd);
          sys_closesocket(fd);
        }
      else
        {
#ifdef DEBUG
          post ("%s_socketreceiver_read: ret = %d", objName, ret);
#endif
          x->sr_inhead += ret;
          if (x->sr_inhead >= INBUFSIZE) x->sr_inhead = 0;
          /* output client's IP and socket no. */
          for(i = 0; i < y->x_nconnections; i++)	/* search for corresponding IP */
            {
              if(y->x_sr[i]->sr_fd == y->x_sock_fd)
                {
                  //                  outlet_symbol(x->x_connectionip, x->x_sr[i].sr_host);
                  /* find sender's ip address and output it */
                  y->x_addrbytes[0].a_w.w_float = (y->x_sr[i]->sr_addr & 0xFF000000)>>24;
                  y->x_addrbytes[1].a_w.w_float = (y->x_sr[i]->sr_addr & 0x0FF0000)>>16;
                  y->x_addrbytes[2].a_w.w_float = (y->x_sr[i]->sr_addr & 0x0FF00)>>8;
                  y->x_addrbytes[3].a_w.w_float = (y->x_sr[i]->sr_addr & 0x0FF);
                  outlet_list(y->x_addrout, &s_list, 4L, y->x_addrbytes);
                  break;
                }
            }
          outlet_float(y->x_sockout, y->x_sock_fd);	/* the socket number */
          tcpserver_socketreceiver_doread(x);
        }
    }
}

static void tcpserver_socketreceiver_free(t_tcpserver_socketreceiver *x)
{
  if (x != NULL)
    {
      sender_destroy(x->sr_sender);
      free(x->sr_inbuf);
      freebytes(x, sizeof(*x));
    }
}

/* ---------------- main tcpserver (send) stuff --------------------- */

static void tcpserver_send_bytes(int client, t_tcpserver *x, int argc, t_atom *argv)
{
  if(x && x->x_sr && x->x_sr[client]) {
    t_atom                  output_atom[3];
    int size=0;
    
    t_sender*sender=sender=x->x_sr[client]->sr_sender;
    int sockfd = x->x_sr[client]->sr_fd;

    if(sender) {
      t_chunk*chunk=chunk_create(argc, argv);
      size=sender_send(sender, chunk);
    }

    SETFLOAT(&output_atom[0], client+1);
    SETFLOAT(&output_atom[1], size);
    SETFLOAT(&output_atom[2], sockfd);
    outlet_anything( x->x_status_outlet, gensym("sent"), 3, output_atom);
  }
}

#ifdef SIOCOUTQ
/* SIOCOUTQ exists only(?) on linux, returns remaining space in the socket's output buffer  */
static int tcpserver_send_buffer_avaliable_for_client(t_tcpserver *x, int client)
{
  int sockfd = x->x_sr[client].sr_fd;
  int result = 0L;

  ioctl(sockfd, SIOCOUTQ, &result);
  return result;
}
#endif // SIOCOUTQ

/* send message to client using socket number */
static void tcpserver_send(t_tcpserver *x, t_symbol *s, int argc, t_atom *argv)
{
  int     i, sockfd;
  int     client = -1;

  if(x->x_nconnections <= 0)
    {
      post("%s_send: no clients connected", objName);
      return;
    }
  if(argc == 0) /* no socket specified: output state of all sockets */
    {
      tcpserver_output_client_state(x, client);
      return;
    }
  /* get socket number of connection (first element in list) */
  if(argv[0].a_type == A_FLOAT)
    {
      sockfd = atom_getfloatarg(0, argc, argv);
      for(i = 0; i < x->x_nconnections; i++) /* check if connection exists */
        {
          if(x->x_sr[i]->sr_fd == sockfd)
            {
              client = i; /* the client we're sending to */
              break;
            }
        }
      if(client == -1)
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
  if (argc < 2) /* nothing to send: output state of this socket */
    {
      tcpserver_output_client_state(x, client+1);
      return;
    }
  tcpserver_send_bytes(client, x, argc-1, &argv[1]);
}

/* disconnect the client at x_sock_fd */
static void tcpserver_disconnect(t_tcpserver *x)
{
  int i, fd;
  t_tcpserver_socketreceiver *y;

  if (x->x_sock_fd >= 0)
    {
      /* find the socketreceiver for this socket */
      for(i = 0; i < x->x_nconnections; i++)
        {
          if(x->x_sr[i]->sr_fd == x->x_sock_fd)
            {
              y = x->x_sr[i];
              fd = y->sr_fd;
              if (y->sr_notifier) (*y->sr_notifier)(x);
              sys_rmpollfn(fd);
              sys_closesocket(fd);
              x->x_sock_fd = -1;
              return;
            }
        }
    }
  post("%s__disconnect: no connection on socket %d", objName, x->x_sock_fd);
}

/* disconnect a client by socket */
static void tcpserver_socket_disconnect(t_tcpserver *x, t_floatarg fsocket)
{
  int sock = (int)fsocket;

  if(x->x_nconnections <= 0)
    {
      post("%s_socket_disconnect: no clients connected", objName);
      return;
    }
  x->x_sock_fd = sock;
  tcpserver_disconnect(x);
}

/* disconnect a client by number */
static void tcpserver_client_disconnect(t_tcpserver *x, t_floatarg fclient)
{
  int client = (int)fclient;

  if(x->x_nconnections <= 0)
    {
      post("%s_client_disconnect: no clients connected", objName);
      return;
    }
  if (!((client > 0) && (client < MAX_CONNECT)))
    {
      post("%s: client %d out of range [1..%d]", objName, client, MAX_CONNECT);
      return;
    }
  --client;/* zero based index*/
  x->x_sock_fd = x->x_sr[client]->sr_fd;
  tcpserver_disconnect(x);
}


/* send message to client using client number
   note that the client numbers might change in case a client disconnects! */
/* clients start at 1 but our index starts at 0 */
static void tcpserver_client_send(t_tcpserver *x, t_symbol *s, int argc, t_atom *argv)
{
  int     client = -1;

  if(x->x_nconnections <= 0)
    {
      post("%s_client_send: no clients connected", objName);
      return;
    }
  if(argc > 0)
    {
      /* get number of client (first element in list) */
      if(argv[0].a_type == A_FLOAT)
        client = atom_getfloatarg(0, argc, argv);
      else
        {
          post("%s_client_send: specify client by number", objName);
          return;
        }
      if (!((client > 0) && (client < MAX_CONNECT)))
        {
          post("%s_client_send: client %d out of range [1..%d]", objName, client, MAX_CONNECT);
          return;
        }
    }
  if (argc > 1)
    {
      --client;/* zero based index*/
      tcpserver_send_bytes(client, x, argc-1, &argv[1]);
      return;
    }
  tcpserver_output_client_state(x, client);
}

static void tcpserver_output_client_state(t_tcpserver *x, int client)
{
  t_atom  output_atom[4];

  if (client == -1)
    {
      /* output parameters of all connections via status outlet */
      for(client = 0; client < x->x_nconnections; client++)
        {
          x->x_sr[client]->sr_fdbuf = tcpserver_get_socket_send_buf_size(x->x_sr[client]->sr_fd);
          SETFLOAT(&output_atom[0], client+1);
          SETFLOAT(&output_atom[1], x->x_sr[client]->sr_fd);
          output_atom[2].a_type = A_SYMBOL;
          output_atom[2].a_w.w_symbol = x->x_sr[client]->sr_host;
          SETFLOAT(&output_atom[3], x->x_sr[client]->sr_fdbuf);
          outlet_anything( x->x_status_outlet, gensym("client"), 4, output_atom);
        }
    }
  else
    {
      client -= 1;/* zero-based client index conflicts with 1-based user index !!! */
      /* output client parameters via status outlet */
      x->x_sr[client]->sr_fdbuf = tcpserver_get_socket_send_buf_size(x->x_sr[client]->sr_fd);
      SETFLOAT(&output_atom[0], client+1);/* user sees client 0 as 1 */
      SETFLOAT(&output_atom[1], x->x_sr[client]->sr_fd);
      output_atom[2].a_type = A_SYMBOL;
      output_atom[2].a_w.w_symbol = x->x_sr[client]->sr_host;
      SETFLOAT(&output_atom[3], x->x_sr[client]->sr_fdbuf);
      outlet_anything( x->x_status_outlet, gensym("client"), 4, output_atom);
    }
}

/* Return the send buffer size of socket */
static int tcpserver_get_socket_send_buf_size(int sockfd)
{
  int                 optVal = 0;
  unsigned int        optLen = sizeof(int);
#ifdef _WIN32
  if (getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (char*)&optVal, &optLen) == SOCKET_ERROR)
    post("%_get_socket_send_buf_size: getsockopt returned %d\n", objName, WSAGetLastError());
#else
  if (getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (char*)&optVal, &optLen) == -1)
    post("%_get_socket_send_buf_size: getsockopt returned %d\n", objName, errno);
#endif
  return  optVal;
}

/* Set the send buffer size of socket, returns actual size */
#ifdef _WIN32
#define TCPSERVER_SOCKET_ERROR SOCKET_ERROR
#else
#define TCPSERVER_SOCKET_ERROR -1
#endif
static int tcpserver_set_socket_send_buf_size(int sockfd, int size)
{
  int                 optVal = size;
  int                 optLen = sizeof(int);
  if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (char*)&optVal, optLen) == TCPSERVER_SOCKET_ERROR)
    {
      post("%s_set_socket_send_buf_size: setsockopt returned %d\n", objName, WSAGetLastError());
      return 0;
    }
  else return (tcpserver_get_socket_send_buf_size(sockfd));
}

/* broadcasts a message to all connected clients */
static void tcpserver_broadcast(t_tcpserver *x, t_symbol *s, int argc, t_atom *argv)
{
  int     client;
  /* enumerate through the clients and send each the message */
  for(client = 0; client < x->x_nconnections; client++)	/* check if connection exists */
    {
      if(x->x_sr[client]->sr_fd >= 0)
        { /* socket exists for this client */
          tcpserver_send_bytes(client, x, argc, argv);
        }
    }
}

/* ---------------- main tcpserver (receive) stuff --------------------- */

static void tcpserver_notify(t_tcpserver *x)
{
  int     i, k;

  /* remove connection from list */
  for(i = 0; i < x->x_nconnections; i++)
    {
      if(x->x_sr[i]->sr_fd == x->x_sock_fd)
        {
          x->x_nconnections--;
          post("%s: \"%s\" removed from list of clients", objName, x->x_sr[i]->sr_host->s_name);
          tcpserver_socketreceiver_free(x->x_sr[i]);
          x->x_sr[i] = NULL;

          /* rearrange list now: move entries to close the gap */
          for(k = i; k < x->x_nconnections; k++)
            {
              x->x_sr[k] = x->x_sr[k + 1];
            }
        }
    }
  outlet_float(x->x_connectout, x->x_nconnections);
}

static void tcpserver_connectpoll(t_tcpserver *x)
{
  struct sockaddr_in  incomer_address;
  unsigned int        sockaddrl = sizeof( struct sockaddr );
  int                 fd = accept(x->x_connectsocket, (struct sockaddr*)&incomer_address, &sockaddrl);
  int                 i;
  int                 optVal;
  unsigned int        optLen = sizeof(int);

  if (fd < 0) post("%s: accept failed", objName);
  else
    {
      t_tcpserver_socketreceiver *y = tcpserver_socketreceiver_new((void *)x,
                                                                   (t_tcpserver_socketnotifier)tcpserver_notify, NULL);/* MP tcpserver_doit isn't used I think...*/
      if (!y)
        {
#ifdef _WIN32
          closesocket(fd);
#else
          close(fd);
#endif
          return;
        }
      y->sr_sender=sender_create(fd);

      sys_addpollfn(fd, (t_fdpollfn)tcpserver_socketreceiver_read, y);
      x->x_nconnections++;
      i = x->x_nconnections - 1;
      x->x_sr[i] = y;
      x->x_sr[i]->sr_host = gensym(inet_ntoa(incomer_address.sin_addr));
      x->x_sr[i]->sr_fd = fd;
      post("%s: accepted connection from %s on socket %d",
           objName, x->x_sr[i]->sr_host->s_name, x->x_sr[i]->sr_fd);
      /* see how big the send buffer is on this socket */
      x->x_sr[i]->sr_fdbuf = 0;
      if (getsockopt(x->x_sr[i]->sr_fd, SOL_SOCKET, SO_SNDBUF, (char*)&optVal, &optLen) == 0)
        {
          x->x_sr[i]->sr_fdbuf = optVal;
        }
      else post("%s_connectpoll: getsockopt returned %d\n", objName, WSAGetLastError());

      outlet_float(x->x_connectout, x->x_nconnections);
      outlet_float(x->x_sockout, x->x_sr[i]->sr_fd);	/* the socket number */
      x->x_sr[i]->sr_addr = ntohl(incomer_address.sin_addr.s_addr);
      x->x_addrbytes[0].a_w.w_float = (x->x_sr[i]->sr_addr & 0xFF000000)>>24;
      x->x_addrbytes[1].a_w.w_float = (x->x_sr[i]->sr_addr & 0x0FF0000)>>16;
      x->x_addrbytes[2].a_w.w_float = (x->x_sr[i]->sr_addr & 0x0FF00)>>8;
      x->x_addrbytes[3].a_w.w_float = (x->x_sr[i]->sr_addr & 0x0FF);
      outlet_list(x->x_addrout, &s_list, 4L, x->x_addrbytes);
    }
}

static void tcpserver_print(t_tcpserver *x)
{
  int     i;

  if(x->x_nconnections > 0)
    {
      post("%s: %d open connections:", objName, x->x_nconnections);
      for(i = 0; i < x->x_nconnections; i++)
        {
          post("        \"%s\" on socket %d",
               x->x_sr[i]->sr_host->s_name, x->x_sr[i]->sr_fd);
        }
    }
  else post("%s: no open connections", objName);
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
#ifdef IRIX
  /* this seems to work only in IRIX but is unnecessary in
     Linux.  Not sure what NT needs in place of this. */
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 0, 0) < 0)
    post("setsockopt failed\n");
#endif
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
  x->x_msgout = outlet_new(&x->x_obj, &s_anything); /* 1st outlet for received data */
  /* streaming protocol */
  if (listen(sockfd, 5) < 0)
    {
      sys_sockerror("tcpserver: listen");
      sys_closesocket(sockfd);
      sockfd = -1;
    }
  else
    {
      sys_addpollfn(sockfd, (t_fdpollfn)tcpserver_connectpoll, x);
      x->x_connectout = outlet_new(&x->x_obj, &s_float); /* 2nd outlet for number of connected clients */
      x->x_sockout = outlet_new(&x->x_obj, &s_float); /* 3rd outlet for socket number of current client */
      x->x_addrout = outlet_new(&x->x_obj, &s_list); /* 4th outlet for ip address of current client */
      x->x_status_outlet = outlet_new(&x->x_obj, &s_anything);/* 5th outlet for everything else */
    }
  x->x_connectsocket = sockfd;
  x->x_nconnections = 0;
  for(i = 0; i < MAX_CONNECT; i++)
    {
      x->x_sr[i] = NULL;
    }
  /* prepare to convert the bytes in the buffer to floats in a list */
  for (i = 0; i < MAX_UDP_RECEIVE; ++i)
    {
      x->x_msgoutbuf[i].a_type = A_FLOAT;
      x->x_msgoutbuf[i].a_w.w_float = 0;
    }
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
        if (x->x_sr[i]->sr_fd >= 0)
          {
            sys_rmpollfn(x->x_sr[i]->sr_fd);
            sys_closesocket(x->x_sr[i]->sr_fd);
          }
      }
    }
  if (x->x_connectsocket >= 0)
    {
      sys_rmpollfn(x->x_connectsocket);
      sys_closesocket(x->x_connectsocket);
    }

}

void tcpserver_setup(void)
{
  tcpserver_class = class_new(gensym(objName),(t_newmethod)tcpserver_new, (t_method)tcpserver_free,
                              sizeof(t_tcpserver), 0, A_DEFFLOAT, 0);
  class_addmethod(tcpserver_class, (t_method)tcpserver_print, gensym("print"), 0);
  class_addmethod(tcpserver_class, (t_method)tcpserver_send, gensym("send"), A_GIMME, 0);
  class_addmethod(tcpserver_class, (t_method)tcpserver_client_send, gensym("client"), A_GIMME, 0);
  class_addmethod(tcpserver_class, (t_method)tcpserver_client_disconnect, gensym("disconnectclient"), A_DEFFLOAT, 0);
  class_addmethod(tcpserver_class, (t_method)tcpserver_socket_disconnect, gensym("disconnectsocket"), A_DEFFLOAT, 0);
  class_addmethod(tcpserver_class, (t_method)tcpserver_broadcast, gensym("broadcast"), A_GIMME, 0);
  class_addlist(tcpserver_class, (t_method)tcpserver_send);
}

/* end of tcpserver.c */
