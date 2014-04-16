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

/* for calloc() */
#include <stdlib.h>

struct _iemnet_notify {
  void*data;
  t_iemnet_notifun fun;
  struct _iemnet_notifier*parent;
  struct _iemnet_notify*next;
};

struct _iemnet_notifier {
  int fd[2];
  struct _iemnet_notify*nodes;
};

typedef struct _iemnet_notifynodes {
  t_iemnet_notify*node;
  struct _iemnet_notifynodes*next;
} t_iemnet_notifynodes;

static t_iemnet_notify*pollqueue=NULL;
static t_iemnet_notifier *masternotifier = NULL;

static t_iemnet_notifynodes*addnodes=NULL;
static t_iemnet_notifynodes*delnodes=NULL;

/* notifies Pd that there is new data to fetch */
void iemnet__notify(t_iemnet_notify*x) {
  write(masternotifier->fd[1], x, sizeof(x));
}

static void iemnet_notifynodes_free(t_iemnet_notifynodes*x) {
  while(x) {
    t_iemnet_notifynodes*next=x->next;
    x->node=NULL; x->next=NULL; freebytes(x, sizeof(*x));
    x=next;
  }
}

/* add pending notify-nodes to the queue, remove pending notify-nodes from the queue; return pointer to updated queue
 * after this operation addqueue/delqueue are consumed (elements have moved to queue, or have been freed
 */
static t_iemnet_notify*iemnet_notifier_update(t_iemnet_notify*queue, t_iemnet_notifynodes*addqueue, t_iemnet_notifynodes*delqueue) {
  t_iemnet_notifynodes*x=NULL;
  //  printf("updating queue %p (%p, %p)\n", queue, addqueue, delqueue);
  /* add elements from addqueue to queue (LATER check for uniqueness) */
  for(x=addqueue; x; x=x->next) {
    t_iemnet_notify*node=x->node;

    node->next=queue;
    queue=node;
  }

  /* del elements found in delqueue from queue, and free both */
  for(x=delqueue; x; x=x->next) {
    t_iemnet_notify*q;
    t_iemnet_notify*last=NULL;
    for(q=queue; q; q=q->next) {
      if(q == x->node) { // found to-be deleted element in queue, now remove it
        if(last) {
          last->next=q->next;
        } else {
          queue=q->next;
        }
        q->fun=NULL;
        q->data=NULL;
        q->next=NULL;
        freebytes(q, sizeof(*q));
        break;
      } else
        last=q;
    }
  }

  /* delete add/delqueue */
  iemnet_notifynodes_free(addqueue);
  iemnet_notifynodes_free(delqueue);
  //  printf("updated queue %p (%p, %p)\n", queue, addqueue, delqueue);

  return queue;
}

static void pollfun(void*z, int fd) {
  char buf[4096];
  int result=-1;
  t_iemnet_notify*q;
  result=read(fd, buf, sizeof(buf));
  pollqueue=iemnet_notifier_update(pollqueue, addnodes, delnodes); addnodes=NULL; delnodes=NULL;
  q=pollqueue;
  //  printf("pollering %p\n", q);
  while(q) {
    t_iemnet_notify*current=q;
    q=q->next;
    //    printf("polling %p: %p(%p) -> %p\n", current, current->fun, current->data, current->next);
    (current->fun)(current->data);
    //    printf("polled  %p: %p(%p) -> %p\n", current, current->fun, current->data, current->next);
  }
  pollqueue=iemnet_notifier_update(pollqueue, addnodes, delnodes); addnodes=NULL; delnodes=NULL;
  //  printf("polldered: %p\n", pollqueue);
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
  masternotifier=(t_iemnet_notifier*)calloc(1, sizeof(t_iemnet_notifier));
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
  t_iemnet_notifynodes*node=(t_iemnet_notifynodes*)getbytes(sizeof(t_iemnet_notifynodes));
  q->fun =fun;
  q->data=data;
  q->parent=notifier;
  q->next=NULL;
  node->node=q;
  //  printf("polladd %p\n", q);

  if(subthread)sys_lock();
  node->next=addnodes;
  addnodes=node;
  if(subthread)sys_unlock();

  //iemnet__notifier_print(notifier);
  //  printf("polladded: %p\n", q);
  return q;
}
void iemnet__notify_remove(t_iemnet_notify*x, int subthread) {
  t_iemnet_notify*q=pollqueue;
  t_iemnet_notify*last=NULL;
  t_iemnet_notifynodes*node=(t_iemnet_notifynodes*)getbytes(sizeof(t_iemnet_notifynodes));
  //iemnet__notifier_print(q->parent);

  node->node=x;

  if(subthread)sys_lock();
  node->next=delnodes;
  delnodes=node;
  if(subthread)sys_unlock();
  //  printf("polldeled: %p\n", x);
}
