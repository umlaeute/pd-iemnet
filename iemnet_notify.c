/* iemnet
 *
 * notify
 *   notifies mai thread that new data has arrived
 *
 *  copyright (c) 2012 IOhannes m zmölnig, IEM
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

/* for pipe() */
#include <unistd.h>

/* for printf() debugging */
#include <stdio.h>

struct _iemnet_notify {
  void*data;
  t_iemnet_notifun fun;
  struct _iemnet_notifier*parent;
  struct _iemnet_notify*next;
};

static t_iemnet_notify*pollqueue=NULL;

struct _iemnet_notifier {
  int fd[2];
  struct _iemnet_notify*nodes;
};

static t_iemnet_notifier *masternotifier = NULL;

/* notifies Pd that there is new data to fetch */
void iemnet__notify(t_iemnet_notify*x) {
  write(masternotifier->fd[1], x, sizeof(x));
}

static void pollfun(void*x, int fd) {
  char buf[4096];
  int result=-1;
  t_iemnet_notify*q;
  result=read(fd, buf, sizeof(buf));
  for(q=pollqueue; q; q=q->next) {
    (q->fun)(q->data);
  }
}

static void iemnet__notifier_print(t_iemnet_notifier*x) {
  t_iemnet_notify*q;
  for(q=pollqueue; q; q=q->next) {
    printf("queue[%p]={fun:%p, data:%p, next:%p}\n", q, q->fun, q->data, q->next);
  }
}
t_iemnet_notifier*iemnet__notify_create(int subthread) {
  if(masternotifier!=NULL)
    return masternotifier;
  masternotifier=(t_iemnet_notifier*)getbytes(sizeof(t_iemnet_notifier));
  if(!pipe(masternotifier->fd)) {
    if(subthread)sys_lock();
    sys_addpollfn(masternotifier->fd[0], pollfun, masternotifier);
    if(subthread)sys_unlock();
    return masternotifier;
  }
  return NULL;
}
void iemnet__notify_destroy(t_iemnet_notifier*x, int subthread) {
  // nada
}

t_iemnet_notify*iemnet__notify_add(t_iemnet_notifier*notifier, t_iemnet_notifun fun, void*data, int subthread) {
  /* add the given receiver to the poll-queue
   * LATER: check whether it's already in there...
   */
  t_iemnet_notify*q=(t_iemnet_notify*)getbytes(sizeof(t_iemnet_notify));
  q->fun =fun;
  q->data=data;
  q->parent=notifier;
  q->next=pollqueue;

  if(subthread)sys_lock();
  pollqueue=q;
  if(subthread)sys_unlock();

  //iemnet__notifier_print(notifier);
  return q;
}
void iemnet__notify_remove(t_iemnet_notify*x, int subthread) {
  t_iemnet_notify*q=pollqueue;
  t_iemnet_notify*last=NULL;
  //iemnet__notifier_print(q->parent);

  if(subthread)sys_lock();
  for(q=pollqueue; q; q=q->next) {
    if(q == x) {
      if(last) {
        last->next=q->next;
      } else {
        pollqueue=q->next;
      }
      if(subthread)sys_unlock();
      q->fun =NULL;
      q->data=NULL;
      q->next=NULL;
      freebytes(q, sizeof(*q));
      return;
    }
    last=q;
  }
  if(subthread)sys_unlock();
}
