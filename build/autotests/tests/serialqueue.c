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
    //    post("producing %d", i);

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

    /* in a non-threaded context, we must not use queue_pop_block() 
     * as we have no way to unblock the queue,
     * resulting in a deadlock
     */
    chunk=queue_pop_noblock(q);
    if(!chunk)
      break;
    if(sizeof(data_t)!=chunk->size) {
      error("size mismatch %d!=%d", sizeof(data_t), chunk->size);
      fail();
    }
    data=chunk->data;
    //    post("consumed %d", data->count);
    iemnet__chunk_destroy(chunk);
  }
  printf("\n");
  return 0;
}

void serialqueue_setup(void) {
  t_iemnet_queue*q=queue_create();
  producer(q, 1000, 1);
  consumer(q);

  queue_destroy(q);
  pass();
}
