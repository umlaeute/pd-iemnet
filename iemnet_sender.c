/* iemnet
 *
 * sender
 *   sends data "chunks" to a socket
 *   possibly threaded
 *
 *  copyright © 2010-2015 IOhannes m zmölnig, IEM
 */

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

#define DEBUGLEVEL 2

#include "iemnet.h"
#include "iemnet_data.h"


#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/types.h>

#ifdef _WIN32
# include <winsock2.h>
# include <ws2tcpip.h> /* for socklen_t */
#else
# include <sys/socket.h>
# include <unistd.h>
# include <fcntl.h>
#endif

#include <pthread.h>

#if IEMNET_HAVE_DEBUG
static int debug_lockcount = 0;
# define LOCK(x) do {if(iemnet_debug(DEBUGLEVEL, __FILE__, __LINE__, __FUNCTION__))post("  LOCKing %p", x); pthread_mutex_lock(x);debug_lockcount++; if(iemnet_debug(DEBUGLEVEL, __FILE__, __LINE__, __FUNCTION__))post("  LOCKed  %p[%d]", x, debug_lockcount); } while(0)
# define UNLOCK(x) do {debug_lockcount--;if(iemnet_debug(DEBUGLEVEL, __FILE__, __LINE__, __FUNCTION__))post("  UNLOCK %p [%d]", x, debug_lockcount); pthread_mutex_unlock(x);}while(0)
#else
# define LOCK(x) pthread_mutex_lock(x)
# define UNLOCK(x) pthread_mutex_unlock(x)
#endif

/* draft:
 *   - there is a sender thread for each open connection
 *   - the main thread just adds chunks to each sender threads processing queue
 *   - the sender thread tries to send the queue as fast as possible
 */

struct _iemnet_sender {
  pthread_t thread;

  int sockfd; /* owned outside; must call iemnet__sender_destroy() before freeing socket yourself */
  t_iemnet_queue*queue;
  int keepsending; /* indicates whether we want to thread to continue or to terminate */
  int isrunning;

  const void*userdata; /* user provided data */
  t_iemnet_sendfunction sendfun; /* user provided send function */

  pthread_mutex_t mtx; /* mutex to protect isrunning,.. */
};

/* the workhorse of the family */


static int iemnet__sender_defaultsend(const void*x, int sockfd,
                                      t_iemnet_chunk*c)
{
  int result = -1;

  struct sockaddr_in to;
  socklen_t tolen = sizeof(to);

  unsigned char*data = c->data;
  unsigned int size = c->size;

  int flags = 0;
#ifdef __linux__
  flags |= MSG_NOSIGNAL;
#endif


  //fprintf(stderr, "sending %d bytes at %x to %d\n", size, data, sockfd);
  if(c->port) {
    DEBUG("%p sending %d bytes to %x:%d @%d", x, size, c->addr, c->port, c->family);

    to.sin_addr.s_addr = htonl(c->addr);
    to.sin_port = htons(c->port);
    to.sin_family = c->family;
    result = sendto(sockfd,
                    (void *)data, size, /* DATA */
                    flags, /* FLAGS */
                    (struct sockaddr *)&to, tolen); /* DESTADDR */
  } else {
    DEBUG("sending %d bytes", size);
    result = send(sockfd,
                  (void *)data, size, /* DATA */
                  flags); /* FLAGS */
  }
  if(result<0) {
    /* broken pipe */
    return 0;
  }

  /* shouldn't we do something with the result here? */
  DEBUG("sent %d bytes", result);
  return 1;
}

static void*iemnet__sender_sendthread(void*arg)
{
  t_iemnet_sender*sender = (t_iemnet_sender*)arg;

  int sockfd = -1;
  t_iemnet_queue*q = NULL;
  t_iemnet_chunk*c = NULL;
  t_iemnet_sendfunction dosend = iemnet__sender_defaultsend;
  const void*userdata = NULL;

  LOCK(&sender->mtx);
  q = sender->queue;
  userdata = sender->userdata;
  if(NULL != sender->sendfun) {
    dosend = sender->sendfun;
  }

  sockfd = sender->sockfd;

  while(sender->keepsending) {
    UNLOCK(&sender->mtx);

    c = queue_pop_block(q);
    if(c) {
      if(!dosend(userdata, sockfd, c)) {
        iemnet__chunk_destroy(c);

        LOCK(&sender->mtx);
        break;
      }
      iemnet__chunk_destroy(c);
      c = NULL;
    }
    LOCK(&sender->mtx);
  }
  sender->isrunning = 0;
  UNLOCK(&sender->mtx);
  DEBUG("send thread terminated");
  return NULL;
}

int iemnet__sender_send(t_iemnet_sender*s, t_iemnet_chunk*c)
{
  t_iemnet_queue*q = 0;
  int size = -1;
  LOCK(&s->mtx);
  q = s->queue;
  if(!s->isrunning) {
    UNLOCK(&s->mtx);
    return -1;
  }
  UNLOCK(&s->mtx);
  if(q) {
    t_iemnet_chunk*chunk = iemnet__chunk_create_chunk(c);
    size = queue_push(q, chunk);
  }
  return size;
}

void iemnet__sender_destroy(t_iemnet_sender*s, int subthread)
{
  /* simple protection against recursive calls:
   * s->keepsending is only set to "0" in here,
   * so if it is false, we know that we are already being called
   */
  DEBUG("destroy sender %x with queue %x (%d) in %sthread", s, s->queue, s->keepsending, subthread?"sub":"main ");
  LOCK(&s->mtx);
  /* check s->isrunning */
  DEBUG("keepsending %d\tisrunning %d", s->keepsending, s->isrunning);
  if(!s->keepsending) {
    UNLOCK(&s->mtx);
    return;
  }
  s->keepsending = 0;

  while(s->isrunning) {
    s->keepsending = 0;
    queue_finish(s->queue);
    UNLOCK(&s->mtx);
    LOCK(&s->mtx);
  }

  UNLOCK(&s->mtx);

  queue_finish(s->queue);
  DEBUG("queue finished");

  pthread_join(s->thread, NULL);
  DEBUG("thread joined");
  queue_destroy(s->queue);

  pthread_mutex_destroy (&s->mtx);

  memset(s, 0, sizeof(t_iemnet_sender));
  s->sockfd = -1;
  free(s);
  s = NULL;
  DEBUG("destroyed sender");
}


t_iemnet_sender*iemnet__sender_create(int sock,
                                      t_iemnet_sendfunction sendfun, const void*userdata,
                                      int subthread)
{
  static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
  t_iemnet_sender*result = (t_iemnet_sender*)calloc(1,
                           sizeof(t_iemnet_sender));
  int res = 0;
  DEBUG("create sender %x in%sthread", result,subthread?"sub":"main ");
  if(NULL == result) {
    DEBUG("create sender failed");
    return NULL;
  }

  result->queue = queue_create();
  result->sockfd = sock;
  result->keepsending = 1;
  result->isrunning = 1;
  result->sendfun = sendfun;
  result->userdata = userdata;
  DEBUG("create_sender queue = %x", result->queue);

  memcpy(&result->mtx, &mtx, sizeof(pthread_mutex_t));
  res = pthread_create(&result->thread, 0, iemnet__sender_sendthread,
                       result);

  if(0 == res) {
  } else {
    /* something went wrong */
    queue_destroy(result->queue);
    free(result);
    return NULL;
  }

  DEBUG("created sender");
  return result;
}

/* coverity[param_set_but_not_used]: as x is there for potentially more specific implentations in the future */
int iemnet__sender_getlasterror(t_iemnet_sender*x)
{
  (void)x; /* ignore unused variable */
#ifdef _WIN32
  return WSAGetLastError();
#endif
  return errno;
}


int iemnet__sender_getsockopt(t_iemnet_sender*s, int level, int optname,
                              void*optval, socklen_t*optlen)
{
  int result = getsockopt(s->sockfd, level, optname, optval, optlen);
  if(result != 0) {
    pd_error(0, "iemnet::sender: getsockopt returned %d",
             iemnet__sender_getlasterror(s));
  }
  return result;
}
int iemnet__sender_setsockopt(t_iemnet_sender*s, int level, int optname,
                              const void*optval, socklen_t optlen)
{
  int result = setsockopt(s->sockfd, level, optname, optval, optlen);
  if(result != 0) {
    pd_error(0, "iemnet::sender: setsockopt returned %d",
             iemnet__sender_getlasterror(s));
  }
  return result;
}



int iemnet__sender_getsize(t_iemnet_sender*x)
{
  int size = -1;
  if(x && x->queue) {
    size = queue_getsize(x->queue);
  }

  return size;
}



static int sock_set_nonblocking(int socket, int nonblocking)
{
#ifdef _WIN32
  u_long modearg = nonblocking;
  if (ioctlsocket(socket, FIONBIO, &modearg) != NO_ERROR) {
    return -1;
  }
#else
  int sockflags = fcntl(socket, F_GETFL, 0);
  if (nonblocking) {
    sockflags |= O_NONBLOCK;
  } else {
    sockflags &= ~O_NONBLOCK;
  }
  if (fcntl(socket, F_SETFL, sockflags) < 0) {
    return -1;
  }
#endif
  return 0;
}

static int conn_in_progress()
{
#ifdef _WIN32
  return (WSAGetLastError() == WSAEWOULDBLOCK);
#else
  return (EINPROGRESS == errno);
#endif
}


int iemnet__connect(int sockfd, const struct sockaddr *addr,
                    socklen_t addrlen, float timeout)
{
  if(timeout<0) {
    return connect(sockfd, addr, addrlen);
  }
  sock_set_nonblocking(sockfd, 1);
  if ((connect(sockfd, addr, addrlen)) < 0) {
    int status;
    struct timeval timeoutval;
    fd_set writefds, errfds;
    if (!conn_in_progress()) {
      return -1;  // break on "real" error
    }

    // block with select using timeout
    if (timeout < 0) {
      timeout = 0;
    }
    timeout *= 0.001; /* seconds -> ms */
    timeoutval.tv_sec = (int)timeout;
    timeoutval.tv_usec = (timeout - timeoutval.tv_sec) * 1000000;
    FD_ZERO(&writefds);
    FD_SET(sockfd, &writefds); // socket is connected when writable
    FD_ZERO(&errfds);
    FD_SET(sockfd, &errfds); // catch exceptions

    status = select(sockfd+1, NULL, &writefds, &errfds, &timeoutval);
    if (status < 0) { // select failed
      fprintf(stderr, "socket_connect: select failed");
      return -1;
    } else if (status == 0) { // connection timed out
#ifdef _WIN32
      WSASetLastError(WSAETIMEDOUT);
#else
      errno = ETIMEDOUT;
#endif
      return -1;
    }

    if (FD_ISSET(sockfd, &errfds)) { // connection failed
      int err;
      socklen_t len = sizeof(err);
      getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (void *)&err, &len);
#ifdef _WIN32
      WSASetLastError(err);
#else
      errno = err;
#endif
      return -1;
    }
  }
  // done, set blocking again
  sock_set_nonblocking(sockfd, 0);
  return 0;
}
