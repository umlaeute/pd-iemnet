#include <common.h>

#include <pthread.h>

#define NUMCHUNKS 1000


typedef union {
  unsigned char cp;
  int count;
} data_t;
static int producer(t_iemnet_queue*q, unsigned int count, unsigned int msec) {
  unsigned int i;
  data_t data;
  for(i=0; i<count; i++) {
    t_iemnet_chunk*chunk=0;
    data.count=i;

    chunk=iemnet__chunk_create_data(sizeof(data), &data.cp);
    queue_push(q, chunk);
    usleep(1000*msec);
  }
  return 0;
}

static int consumer(t_iemnet_queue*q) {
  t_iemnet_chunk*chunk=NULL;
  while(1) {
    data_t*data=NULL;
    chunk=queue_pop_block(q);
    if(!chunk)
      break;
    if(sizeof(data_t)!=chunk->size) {
      error("size mismatch %d!=%d", sizeof(data_t), chunk->size);
      fail();
    }
    data=chunk->data;
    //printf("%d ", data->count);
    iemnet__chunk_destroy(chunk);
  }
  printf("\n");
  return 0;
}
static void* consumer_thread(void*qq) {
  t_iemnet_queue*q=(t_iemnet_queue*)qq;
  consumer(q);
  return NULL;
}

void threadedqueue_setup(void) {
  pthread_t thread;
  pthread_attr_t  threadattr;

  t_iemnet_queue*q=queue_create();

  /* prepare child thread */
  if(pthread_attr_init(&threadattr) < 0) {
    error("warning: could not prepare child thread");
    fail();
  }
  if(pthread_attr_setdetachstate(&threadattr, PTHREAD_CREATE_DETACHED) < 0) {
    error("warning: could not prepare child thread...");
    fail();
  }

  if(pthread_create(&thread, &threadattr, consumer_thread, q) < 0) {
    error("warning: could not create child thread");
    fail();
  }


  producer(q, 1000, 1);


  queue_destroy(q);
  pass();
}
