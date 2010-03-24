/* queue.c
 *  copyright (c) 2010 IOhannes m zmölnig, IEM
 *   using code found at http://newsgroups.derkeiler.com/Archive/Comp/comp.programming.threads/2008-02/msg00502.html
 */


#include "iemnet.h"

#include "s_stuff.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <sys/types.h>

#ifdef _WIN32
# include <winsock2.h>
# include <ws2tcpip.h> /* for socklen_t */
#else
# include <sys/socket.h>
#endif

#include <pthread.h>


//#define INBUFSIZE 4096L /* was 4096: size of receiving data buffer */
#define INBUFSIZE 65536L /* was 4096: size of receiving data buffer */


/* data handling */

typedef struct _iemnet_floatlist {
  t_atom*argv;
  size_t argc;

  size_t size; // real size (might be bigger than argc)
} t_iemnet_floatlist;

t_iemnet_floatlist*iemnet__floatlist_init(t_iemnet_floatlist*cl) {
  unsigned int i;
  if(NULL==cl)return NULL;
  for(i=0; i<cl->size; i++)
    SETFLOAT((cl->argv+i), 0.f);

  return cl;
}

void iemnet__floatlist_destroy(t_iemnet_floatlist*cl) {
  if(NULL==cl)return;
  if(cl->argv) freebytes(cl->argv, sizeof(t_atom)*cl->size);
  cl->argv=NULL;
  cl->argc=0;
  cl->size=0;

  freebytes(cl, sizeof(t_iemnet_floatlist));
}

t_iemnet_floatlist*iemnet__floatlist_create(unsigned int size) {
  t_iemnet_floatlist*result=(t_iemnet_floatlist*)getbytes(sizeof(t_iemnet_floatlist));
  if(NULL==result)return NULL;

  result->argv = (t_atom*)getbytes(size*sizeof(t_atom));
  if(NULL==result->argv) {
    iemnet__floatlist_destroy(result);
    return NULL;
  }

  result->argc=size;
  result->size=size;

  result=iemnet__floatlist_init(result);

  return result;
}

t_iemnet_floatlist*iemnet__floatlist_resize(t_iemnet_floatlist*cl, unsigned int size) {
  t_atom*tmp;
  if(size<=cl->size) {
    cl->argc=size;
    return cl;
  }

  tmp=(t_atom*)getbytes(size*sizeof(t_atom));
  if(NULL==tmp) return NULL;

  freebytes(cl->argv, sizeof(t_atom)*cl->size);

  cl->argv=tmp;
  cl->argc=cl->size=size;

  cl=iemnet__floatlist_init(cl);

  return cl;
}



void iemnet__chunk_destroy(t_iemnet_chunk*c) {
  if(NULL==c)return;

  if(c->data)freebytes(c->data, c->size*sizeof(unsigned char));

  c->data=NULL;
  c->size=0;

  freebytes(c, sizeof(t_iemnet_chunk));
}

t_iemnet_chunk* iemnet__chunk_create_empty(int size) {
  t_iemnet_chunk*result=(t_iemnet_chunk*)getbytes(sizeof(t_iemnet_chunk));
  if(result) {
    result->size=size;
    result->data=(unsigned char*)getbytes(sizeof(unsigned char)*size); 

    if(NULL == result->data) {
      result->size=0;
      iemnet__chunk_destroy(result);
      return NULL;
    }

    memset(result->data, 0, result->size);
  }
  return result;
}

t_iemnet_chunk* iemnet__chunk_create_data(int size, unsigned char*data) {
  t_iemnet_chunk*result=(t_iemnet_chunk*)getbytes(sizeof(t_iemnet_chunk));
  if(result) {
    result->size=size;
    result->data=(unsigned char*)getbytes(sizeof(unsigned char)*size); 

    if(NULL == result->data) {
      result->size=0;
      iemnet__chunk_destroy(result);
      return NULL;
    }

    memcpy(result->data, data, result->size);
  }
  return result;
}


t_iemnet_chunk* iemnet__chunk_create_list(int argc, t_atom*argv) {
  t_iemnet_chunk*result=(t_iemnet_chunk*)getbytes(sizeof(t_iemnet_chunk));
  int i;
  if(NULL==result)return NULL;

  result->size=argc;
  result->data=(unsigned char*)getbytes(sizeof(unsigned char)*argc);

  if(NULL == result->data) {
    result->size=0;
    iemnet__chunk_destroy(result);
    return NULL;
  }

  for(i=0; i<argc; i++) {
    unsigned char c = atom_getint(argv);
    result->data[i]=c;
    argv++;
  }

  return result;
}

t_iemnet_chunk*iemnet__chunk_create_chunk(t_iemnet_chunk*c) {
  t_iemnet_chunk*result=NULL;
  if(NULL==c)return NULL;

  result=(t_iemnet_chunk*)getbytes(sizeof(t_iemnet_chunk));

  result->size=c->size;
  result->data=(unsigned char*)getbytes(sizeof(unsigned char)*(result->size));
  if(NULL == result->data) {
    result->size=0;
    iemnet__chunk_destroy(result);
    return NULL;
  }

  memcpy(result->data, c->data, result->size);

  return result;
}


t_iemnet_floatlist*iemnet__chunk2list(t_iemnet_chunk*c, t_iemnet_floatlist*dest) {
  unsigned int i;
  if(NULL==c)return NULL;
  dest=iemnet__floatlist_resize(dest, c->size);
  if(NULL==dest)return NULL;

  for(i=0; i<c->size; i++) {
    dest->argv[i].a_w.w_float = c->data[i];
  }

  return dest;
}


/* queue handling */
typedef struct _node {
  struct _node* next;
  t_iemnet_chunk*data;
} t_node;

typedef struct _queue {
  t_node* head; /* = 0 */
  t_node* tail; /* = 0 */
  pthread_mutex_t mtx;
  pthread_cond_t cond;

  int done; // in cleanup state
  int size;
} t_queue;


int queue_push(
                      t_queue* const _this,
                      t_iemnet_chunk* const data
                      ) {
  t_node* tail;
  t_node* n=NULL;
  int size=_this->size;

  if(NULL == data) return size;
  //fprintf(stderr, "pushing %d bytes\n", data->size);

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


  //fprintf(stderr, "pushed %d bytes\n", data->size);

  pthread_mutex_unlock(&_this->mtx);
  pthread_cond_signal(&_this->cond);

  return size;
}

t_iemnet_chunk* queue_pop_block(
                   t_queue* const _this
                   ) {
  t_node* head=0;
  t_iemnet_chunk*data=0;
  pthread_mutex_lock(&_this->mtx);
  while (! (head = _this->head)) {
    if(_this->done) {
      pthread_mutex_unlock(&_this->mtx);
      return NULL;
    }
    else {
      pthread_cond_wait(&_this->cond, &_this->mtx);
    }
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
  return data;
}

t_iemnet_chunk* queue_pop_noblock(
                   t_queue* const _this
                   ) {
  t_node* head=0;
  t_iemnet_chunk*data=0;
  pthread_mutex_lock(&_this->mtx);
  if (! (head = _this->head)) {
    // empty head
    pthread_mutex_unlock(&_this->mtx);
    return NULL;
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
  return data;
}

t_iemnet_chunk* queue_pop(t_queue* const _this) {
  return queue_pop_block(_this);
}


void queue_finish(t_queue* q) {
  if(NULL==q) 
    return;
  q->done=1;
  pthread_cond_signal(&q->cond);
}

void queue_destroy(t_queue* q) {
  t_iemnet_chunk*c=NULL;
  if(NULL==q) 
    return;

  queue_finish(q);

  /* remove all the chunks from the queue */
  while(NULL!=(c=queue_pop_noblock(q))) {
    iemnet__chunk_destroy(c);
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




 /* draft:
  *   - there is a sender thread for each open connection
  *   - the main thread just adds chunks to each sender threads processing queue
  *   - the sender thread tries to send the queue as fast as possible
  */

struct _iemnet_sender {
  pthread_t thread;

  int sockfd; /* owned outside; must call iemnet__sender_destroy() before freeing socket yourself */
  t_queue*queue;
  int cont; // indicates whether we want to thread to continue or to terminate
};

/* the workhorse of the family */

static int iemnet__sender_dosend(int sockfd, t_queue*q) {
  t_iemnet_chunk*c=queue_pop(q);
  if(c) {
    unsigned char*data=c->data;
    unsigned int size=c->size;

    fprintf(stderr, "sending %d bytes at %x to %d\n", size, data, sockfd);

    int result = send(sockfd, data, size, 0);
    // shouldn't we do something with the result here?
    iemnet__chunk_destroy(c);
  } else {
    return 0;
  }
  return 1;
}

static void*iemnet__sender_sendthread(void*arg) {
  t_iemnet_sender*sender=(t_iemnet_sender*)arg;

  int sockfd=sender->sockfd;
  t_queue*q=sender->queue;

  while(sender->cont) {
    if(!iemnet__sender_dosend(sockfd, q))break;
  }
  fprintf(stderr, "write thread terminated\n");
  return NULL;
}

int iemnet__sender_send(t_iemnet_sender*s, t_iemnet_chunk*c) {
  t_queue*q=s->queue;
  int size=0;
  if(q) {
    t_iemnet_chunk*chunk=iemnet__chunk_create_chunk(c);
    size = queue_push(q, chunk);
  }
  return size;
}

void iemnet__sender_destroy(t_iemnet_sender*s) {
  s->cont=0;
  queue_finish(s->queue);
  s->sockfd = -1;
  pthread_join(s->thread, NULL);
  queue_destroy(s->queue);

  freebytes(s, sizeof(t_iemnet_sender));
  s=NULL;
}
t_iemnet_sender*iemnet__sender_create(int sock) {
  t_iemnet_sender*result=(t_iemnet_sender*)getbytes(sizeof(t_iemnet_sender));
  int res=0;

  if(NULL==result)return NULL;

  result->queue = queue_create();
  result->sockfd = sock;
  result->cont =1;

  res=pthread_create(&result->thread, 0, iemnet__sender_sendthread, result);

  if(0==res) {

  } else {
    // something went wrong
  }

  return result;
}

int iemnet__sender_getlasterror(t_iemnet_sender*x) {
  x=NULL;
#ifdef _WIN32
  return WSAGetLastError();
#endif
  return errno;
}


int iemnet__sender_getsockopt(t_iemnet_sender*s, int level, int optname, void      *optval, socklen_t*optlen) {
  int result=getsockopt(s->sockfd, level, optname, optval, optlen);
  if(result!=0) {
    post("%s: getsockopt returned %d", __FUNCTION__, iemnet__sender_getlasterror(s));
  }
  return result;
}
int iemnet__sender_setsockopt(t_iemnet_sender*s, int level, int optname, const void*optval, socklen_t optlen) {
  int result=setsockopt(s->sockfd, level, optname, optval, optlen);
  if(result!=0) {
    post("%s: setsockopt returned %d", __FUNCTION__, iemnet__sender_getlasterror(s));
  }
  return result;

}



struct _iemnet_receiver {
  pthread_t thread;
  int sockfd; /* owned outside; you must call iemnet__receiver_destroy() before freeing socket yourself */
  void*owner;
  t_iemnet_chunk*data;
  t_iemnet_receivecallback callback;
  t_queue*queue;
  int running;
  t_clock *clock;
  t_iemnet_floatlist*flist;
};


/* the workhorse of the family */
static void*iemnet__receiver_readthread(void*arg) {
  int result = 0;
  t_iemnet_receiver*receiver=(t_iemnet_receiver*)arg;

  int sockfd=receiver->sockfd;
  t_queue*q=receiver->queue;

  unsigned char data[INBUFSIZE];
  unsigned int size=INBUFSIZE;

  unsigned int i=0;
  for(i=0; i<size; i++)data[i]=0;
  receiver->running=1;
  while(1) {
    fprintf(stderr, "reading %d bytes...\n", size);
    result = recv(sockfd, data, size, 0);
    fprintf(stderr, "read %d bytes...\n", result);

    if(result<=0)break;
    t_iemnet_chunk*c = iemnet__chunk_create_data(result, data);

    queue_push(q, c);

    if(receiver->clock)clock_delay(receiver->clock, 0);
  }


  if(result>=0)
    if(receiver->clock)clock_delay(receiver->clock, 0);

  receiver->running=0;


  fprintf(stderr, "read thread terminated\n");
  return NULL;
}

#define WHERE fprintf(stderr, "%s:%d", __FUNCTION__, __LINE__)
static void iemnet__receiver_tick(t_iemnet_receiver *x)
{
  WHERE; fprintf(stderr, "\treceiver=%x", x);
  // received data
  t_iemnet_chunk*c=queue_pop_noblock(x->queue);
  WHERE; fprintf(stderr, "\tchunk=%x", c);
  while(NULL!=c) {
    x->flist = iemnet__chunk2list(c, x->flist);
    (x->callback)(x->owner, x->sockfd, x->flist->argc, x->flist->argv);
    iemnet__chunk_destroy(c);
    c=queue_pop_noblock(x->queue);
  }

  WHERE; fprintf(stderr, "\trunning=%d", x->running);

  if(!x->running) {
    // read terminated
    x->callback(x->owner, x->sockfd, 0, NULL);
  }
  WHERE; fprintf(stderr, "\ttick done\n");
}


t_iemnet_receiver*iemnet__receiver_create(int sock, void*owner, t_iemnet_receivecallback callback) {
  t_iemnet_receiver*rec=(t_iemnet_receiver*)getbytes(sizeof(t_iemnet_receiver));
  //fprintf(stderr, "new receiver for %d\t%x\t%x\n", sock, owner, callback);
  if(rec) {
    t_iemnet_chunk*data=iemnet__chunk_create_empty(INBUFSIZE);
    int res=0;
    if(NULL==data) {
      iemnet__receiver_destroy(rec);
      return NULL;
    }
    rec->sockfd=sock;
    rec->owner=owner;
    rec->data=data;
    rec->callback=callback;
    rec->flist=iemnet__floatlist_create(1024);

    rec->queue = queue_create();
    rec->clock = clock_new(rec, (t_method)iemnet__receiver_tick);
    rec->running=1;
    res=pthread_create(&rec->thread, 0, iemnet__receiver_readthread, rec);
  }
  //fprintf(stderr, "new receiver created\n");

  return rec;
}
void iemnet__receiver_destroy(t_iemnet_receiver*rec) {
  if(NULL==rec)return;
  if(rec->data)iemnet__chunk_destroy(rec->data);
  if(rec->flist)iemnet__floatlist_destroy(rec->flist);
  clock_free(rec->clock);
  sys_closesocket(rec->sockfd);

  rec->sockfd=0;
  fprintf(stderr, "receiverdestroy join thread\n");
  pthread_join(rec->thread, NULL);
  fprintf(stderr, "receiverdestroy joined thread\n");
  rec->owner=NULL;
  rec->data=NULL;
  rec->callback=NULL;
  rec->clock=NULL;
  rec->flist=NULL;

  freebytes(rec, sizeof(t_iemnet_receiver));
  rec=NULL;
}
