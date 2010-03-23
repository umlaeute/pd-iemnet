#include "m_pd.h"

#include <sys/socket.h>

typedef struct _iemnet_chunk {
  unsigned char* data;
  size_t size;
} t_iemnet_chunk;

void iemnet__chunk_destroy(t_iemnet_chunk*);
t_iemnet_chunk*iemnet__chunk_create_empty(int);
t_iemnet_chunk*iemnet__chunk_create_list(int, t_atom*);
t_iemnet_chunk*iemnet__chunk_create_chunk(t_iemnet_chunk*);

t_atom*iemnet__chunk2list(t_iemnet_chunk*);

/* sender */
#define t_iemnet_sender struct _iemnet_sender
EXTERN_STRUCT _iemnet_sender;

t_iemnet_sender*iemnet__sender_create(int sock);
void iemnet__sender_destroy(t_iemnet_sender*);

int iemnet__sender_send(t_iemnet_sender*, t_iemnet_chunk*);

int iemnet__sender_getlasterror(t_iemnet_sender*);
int iemnet__sender_getsockopt(t_iemnet_sender*, int level, int optname, void      *optval, socklen_t*optlen);
int iemnet__sender_setsockopt(t_iemnet_sender*, int level, int optname, const void*optval, socklen_t optlen);


/* receiver */
#define t_iemnet_receiver struct _iemnet_receiver
EXTERN_STRUCT _iemnet_receiver;

typedef void (*t_iemnet_receivecallback)(void*x, int, t_iemnet_chunk*);

/**
 * create a receiver object: whenever something is received on the socket,
 * the callback is called with the payload
 */
t_iemnet_receiver*iemnet__receiver_create(int sock, void*owner, t_iemnet_receivecallback callback);
void iemnet__receiver_destroy(t_iemnet_receiver*);
