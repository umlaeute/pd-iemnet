/* iemnet
 *
 * sender
 *   sends data "chunks" to a socket
 *   possibly threaded
 *
 *  copyright (c) 2010 IOhannes m zmölnig, IEM
 */

//#define DEBUG

#include "iemnet.h"
#include "iemnet_data.h"


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


 /* draft:
  *   - there is a sender thread for each open connection
  *   - the main thread just adds chunks to each sender threads processing queue
  *   - the sender thread tries to send the queue as fast as possible
  */

struct _iemnet_sender {
  pthread_t thread;

  int sockfd; /* owned outside; must call iemnet__sender_destroy() before freeing socket yourself */
  t_iemnet_queue*queue;
  int keepsending; // indicates whether we want to thread to continue or to terminate
  int isrunning;
};

/* the workhorse of the family */

static int iemnet__sender_dosend(int sockfd, t_iemnet_queue*q) {
  t_iemnet_chunk*c=queue_pop_block(q);
  if(c) {
    unsigned char*data=c->data;
    unsigned int size=c->size;
    int result=-1;
    //    fprintf(stderr, "sending %d bytes at %x to %d\n", size, data, sockfd);
    DEBUG("sending %d bytes", size);
    result = send(sockfd, data, size, 0);
    if(result<0) {
      // broken pipe
      return 0;
    }

    // shouldn't we do something with the result here?
    DEBUG("sent %d bytes", result);
    iemnet__chunk_destroy(c);
  } else {
    return 0;
  }
  return 1;
}

static void*iemnet__sender_sendthread(void*arg) {
  t_iemnet_sender*sender=(t_iemnet_sender*)arg;

  int sockfd=sender->sockfd;
  t_iemnet_queue*q=sender->queue;

  while(sender->keepsending) {
    if(!iemnet__sender_dosend(sockfd, q))break;
  }
  sender->isrunning=0;
  DEBUG("send thread terminated");
  return NULL;
}

int iemnet__sender_send(t_iemnet_sender*s, t_iemnet_chunk*c) {
  t_iemnet_queue*q=s->queue;
  int size=-1;
  if(!s->isrunning)return -1;
  if(q) {
    t_iemnet_chunk*chunk=iemnet__chunk_create_chunk(c);
    size = queue_push(q, chunk);
  }
  return size;
}

void iemnet__sender_destroy(t_iemnet_sender*s) {
  /* simple protection against recursive calls:
   * s->keepsending is only set to "0" in here, 
   * so if it is false, we know that we are already being called
   */
  if(!s->keepsending)return;

  DEBUG("destroy sender %x", s);
  s->keepsending=0;
  queue_finish(s->queue);
  DEBUG("queue finished");
  s->sockfd = -1;
  pthread_join(s->thread, NULL);
  DEBUG("thread joined");
  queue_destroy(s->queue);
  freebytes(s, sizeof(t_iemnet_sender));
  s=NULL;
  DEBUG("destroyed sender");
}
t_iemnet_sender*iemnet__sender_create(int sock) {
  t_iemnet_sender*result=(t_iemnet_sender*)getbytes(sizeof(t_iemnet_sender));
  int res=0;
  DEBUG("create sender %x", result);
  if(NULL==result){
    DEBUG("create sender failed");
    return NULL;
  }

  result->queue = queue_create();
  result->sockfd = sock;
  result->keepsending =1;
  result->isrunning=1;

  res=pthread_create(&result->thread, 0, iemnet__sender_sendthread, result);

  if(0==res) {

  } else {
    // something went wrong
  }

  DEBUG("created sender");
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
