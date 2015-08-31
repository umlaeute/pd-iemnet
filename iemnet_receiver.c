/* iemnet
 *
 * receiver
 *   receives data "chunks" from a socket
 *
 *  copyright (c) 2010-2015 IOhannes m zmölnig, IEM
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

#define DEBUGLEVEL 4

#include "iemnet.h"
#include "iemnet_data.h"

#include <stdlib.h>
#include <errno.h>

#define INBUFSIZE 65536L /* was 4096: size of receiving data buffer */

struct _iemnet_receiver {
  int sockfd; /* owned outside; you must call iemnet__receiver_destroy() before freeing socket yourself */
  void*userdata;
  t_iemnet_receivecallback callback;
};


static void pollfun(void*z, int fd) {
  // read data from socket and call callback
  t_iemnet_receiver*rec=(t_iemnet_receiver*)z;

  unsigned char data[INBUFSIZE];
  unsigned int size=INBUFSIZE;
  t_iemnet_chunk*chunk=NULL;
  int result = 0;
  int local_errno = 0;

  struct sockaddr_in  from;
  socklen_t           fromlen = sizeof(from);

  int recv_flags=0;
#ifdef MSG_DONTWAIT
  recv_flags|=MSG_DONTWAIT;
#endif
  errno=0;
  result = recvfrom(rec->sockfd, data, size, recv_flags, (struct sockaddr *)&from, &fromlen);
  local_errno=errno;
  //fprintf(stderr, "read %d bytes...\n", result);
  DEBUG("recvfrom %d bytes: %d %p %d", result, rec->sockfd, data, size);
  DEBUG("errno=%d", local_errno);
  chunk = iemnet__chunk_create_dataaddr(result, (result>0)?data:NULL, &from);

  // call the callback with a NULL-chunk to signal a disconnect event.
  (rec->callback)(rec->userdata, chunk);

  iemnet__chunk_destroy(chunk);
}

t_iemnet_receiver*iemnet__receiver_create(int sock, void*userdata, t_iemnet_receivecallback callback, int subthread) {
  t_iemnet_receiver*rec=(t_iemnet_receiver*)malloc(sizeof(t_iemnet_receiver));

  DEBUG("create new receiver for 0x%X:%d", userdata, sock);
  //fprintf(stderr, "new receiver for %d\t%x\t%x\n", sock, userdata, callback);
  if(rec) {
    rec->sockfd=sock;
    rec->userdata=userdata;
    rec->callback=callback;

    if(subthread)sys_lock();
    sys_addpollfn(sock, pollfun, rec);
    if(subthread)sys_unlock();

  }
  //fprintf(stderr, "new receiver created\n");

  return rec;
}
void iemnet__receiver_destroy(t_iemnet_receiver*rec, int subthread) {
  int sockfd;
  if(NULL==rec)return;

  sockfd=rec->sockfd;

  if(subthread)sys_lock();
  sys_rmpollfn(rec->sockfd);

  // FIXXME: read any remaining bytes from the socket

  if(subthread)sys_unlock();

  DEBUG("[%p] really destroying receiver %d", sockfd);
  //iemnet__closesocket(sockfd);
  DEBUG("[%p] closed socket %d", rec, sockfd);

  rec->sockfd=-1;
  rec->userdata=NULL;
  rec->callback=NULL;

  free(rec);
  rec=NULL;
}


/* just dummy, since we don't maintain a queue any more */
int iemnet__receiver_getsize(t_iemnet_receiver*x) {
  if(x)
    return 0;
  return -1;
}
