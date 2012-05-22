#include <common.h>

#define NUMCHUNKS 1000

static int producer(t_iemnet_queue*q, unsigned int count) {
  unsigned int i;
  union {
    unsigned char cp;
    int count;
  } data;
  for(i=0; i<count; i++) {
    t_iemnet_chunk*chunk=0;
    data.count=count;

    chunk=iemnet__chunk_create_data(sizeof(data), &data.cp);
    queue_push(q, chunk);
    usleep(1000);
  }
  return 0;
}

static int consumer(t_iemnet_queue*q) {
  return 0;
}
static int consumer_thread(void*qq) {
  t_iemnet_queue*q=(t_iemnet_queue)qq;
  return consumer(q);
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


  producer(q, 1000);


  queue_destroy(q);
  pass();
}
