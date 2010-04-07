/* iemnet
 *  copyright (c) 2010 IOhannes m zmölnig, IEM
 */

//#define DEBUG

#include "iemnet.h"
#include "iemnet_data.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <pthread.h>

#define INBUFSIZE 65536L /* was 4096: size of receiving data buffer */


struct _iemnet_receiver {
  pthread_t thread;
  int sockfd; /* owned outside; you must call iemnet__receiver_destroy() before freeing socket yourself */
  void*userdata;
  t_iemnet_chunk*data;
  t_iemnet_receivecallback callback;
  t_iemnet_queue*queue;
  int running;
  t_clock *clock;

  int keepreceiving;

  int newdataflag;
  pthread_mutex_t newdatamtx;
};

/* notifies Pd that there is new data to fetch */
static void iemnet_signalNewData(t_iemnet_receiver*x) {
  int already=0;
  pthread_mutex_lock(&x->newdatamtx);
  already=x->newdataflag;
  x->newdataflag=1;

  /* don't schedule ticks at the end of life */
  if(x->sockfd<0)already=1;

  pthread_mutex_unlock(&x->newdatamtx);

  if(already)return;
  sys_lock();
  if(x->clock)clock_delay(x->clock, 0);
  sys_unlock();
}


/* the workhorse of the family */
static void*iemnet__receiver_readthread(void*arg) {
  int result = 0;
  t_iemnet_receiver*receiver=(t_iemnet_receiver*)arg;

  int sockfd=receiver->sockfd;
  t_iemnet_queue*q=receiver->queue;

  unsigned char data[INBUFSIZE];
  unsigned int size=INBUFSIZE;

  struct sockaddr_in  from;
  socklen_t           fromlen = sizeof(from);

  unsigned int i=0;
  for(i=0; i<size; i++)data[i]=0;
  receiver->running=1;
  while(1) {
    t_iemnet_chunk*c=NULL;
    fromlen = sizeof(from);
    //fprintf(stderr, "reading %d bytes...\n", size);
    //result = recv(sockfd, data, size, 0);
    result = recvfrom(sockfd, data, size, 0, (struct sockaddr *)&from, &fromlen);
    //fprintf(stderr, "read %d bytes...\n", result);
    
    if(result<=0)break;
    c= iemnet__chunk_create_dataaddr(result, data, &from);
    
    queue_push(q, c);

    iemnet_signalNewData(receiver);

  }
  if(result>=0)iemnet_signalNewData(receiver);

  receiver->running=0;

  //fprintf(stderr, "read thread terminated\n");
  return NULL;
}

/* callback from Pd's main thread to fetch queued data */
static void iemnet__receiver_tick(t_iemnet_receiver *x)
{
  // received data
  t_iemnet_chunk*c=queue_pop_noblock(x->queue);
  while(NULL!=c) {
    (x->callback)(x->userdata, c);
    iemnet__chunk_destroy(c);
    c=queue_pop_noblock(x->queue);
  }
	DEBUG("tick cleanup");
  pthread_mutex_lock(&x->newdatamtx);
  x->newdataflag=0;
  pthread_mutex_unlock(&x->newdatamtx);
	
	DEBUG("tick running %d", x->running);
  if(!x->running) {
    // read terminated
    
    /* keepreceiving is set, if receiver is not yet in shutdown mode */
    if(x->keepreceiving) 
      x->callback(x->userdata, NULL);
  }
	DEBUG("tick DONE");
}

int iemnet__receiver_getsize(t_iemnet_receiver*x) {
  int size=-1;
  if(x && x->queue)
    size=queue_getsize(x->queue);

  return size;
}


t_iemnet_receiver*iemnet__receiver_create(int sock, void*userdata, t_iemnet_receivecallback callback) {
  static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
  t_iemnet_receiver*rec=(t_iemnet_receiver*)getbytes(sizeof(t_iemnet_receiver));
  DEBUG("create new receiver for 0x%X:%d", userdata, sock);
  //fprintf(stderr, "new receiver for %d\t%x\t%x\n", sock, userdata, callback);
  if(rec) {
    t_iemnet_chunk*data=iemnet__chunk_create_empty(INBUFSIZE);
    int res=0;
    if(NULL==data) {
      iemnet__receiver_destroy(rec);
      DEBUG("create receiver failed");
      return NULL;
    }
    rec->keepreceiving=1;
    rec->sockfd=sock;
    rec->userdata=userdata;
    rec->data=data;
    rec->callback=callback;

    memcpy(&rec->newdatamtx , &mtx, sizeof(pthread_mutex_t));
    rec->newdataflag=0;

    rec->queue = queue_create();
    rec->clock = clock_new(rec, (t_method)iemnet__receiver_tick);
    rec->running=1;
    res=pthread_create(&rec->thread, 0, iemnet__receiver_readthread, rec);
  }
  //fprintf(stderr, "new receiver created\n");

  return rec;
}

void iemnet__receiver_destroy(t_iemnet_receiver*rec) {
  static int instance=0;
  int inst=instance++;
  return;

  int sockfd;
  DEBUG("[%d] destroy receiver %x", inst, rec);
  if(NULL==rec)return;
  if(!rec->keepreceiving)return;
  rec->keepreceiving=0;


  sockfd=rec->sockfd;
  rec->sockfd=-1;

  DEBUG("[%d] really destroying receiver %x -> %d", inst, rec, sockfd);

  if(sockfd>=0) {
    shutdown(sockfd, 2); /* needed on linux, since the recv won't shutdown on sys_closesocket() alone */
    sys_closesocket(sockfd); 
  }
  DEBUG("[%d] closed socket %d", inst, sockfd);

  pthread_join(rec->thread, NULL);


  // empty the queue
  DEBUG("[%d] tick %d", inst, rec->running);
  iemnet__receiver_tick(rec);
  queue_destroy(rec->queue);  
  DEBUG("[%d] tack", inst);

  if(rec->data)iemnet__chunk_destroy(rec->data);

  pthread_mutex_destroy(&rec->newdatamtx);

  clock_free(rec->clock);
  rec->clock=NULL;

  rec->userdata=NULL;
  rec->data=NULL;
  rec->callback=NULL;
	rec->queue=NULL;

  freebytes(rec, sizeof(t_iemnet_receiver));
  rec=NULL;
  DEBUG("[%d] destroyed receiver", inst);
}
