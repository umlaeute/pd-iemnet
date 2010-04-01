/* *********************************************+
 * iemnet
 *     networking for Pd
 *
 *  (c) 2010 IOhannes m zmölnig
 *           Institute of Electronic Music and Acoustics (IEM)
 *           University of Music and Dramatic Arts (KUG), Graz, Austria
 *
 * based on net/ library by Martin Peach
 * based on maxlib by Olaf Matthes
 */

/* ---------------------------------------------------------------------------- */

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
/* along with this program; if not, write to the Free Software                  */
/* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.  */
/*                                                                              */

/* ---------------------------------------------------------------------------- */

#ifndef INCLUDE_IEMNET_H_
#define INCLUDE_IEMNET_H_

#include "m_pd.h"

/* from s_stuff.h */
typedef void (*t_fdpollfn)(void *ptr, int fd);
EXTERN void sys_closesocket(int fd);
EXTERN void sys_sockerror(char *s);
EXTERN void sys_addpollfn(int fd, t_fdpollfn fn, void *ptr);
EXTERN void sys_rmpollfn(int fd);



#ifdef _WIN32
# include <winsock2.h>
# include <ws2tcpip.h>
#else
# include <netdb.h>
# include <arpa/inet.h>
# include <sys/socket.h>
# include <sys/types.h>
#endif

/* iemnet_data.c */

/**
 * chunk of data as sent to a socket or received from it
 * for received data, this might additionally hold the originator (if available)
 */
typedef struct _iemnet_chunk {
  unsigned char* data;
  size_t size;

  long addr;
  unsigned short port;
} t_iemnet_chunk;

/**
 * free a "chunk" (de-allocate memory,...)
 */
void iemnet__chunk_destroy(t_iemnet_chunk*);

/**
 * initialize a "chunk" (allocate memory,...) of fixed size
 * receiver address will be set to 0
 *
 * \param size of the chunk (data will be zeroed out)
 * \return a new chunk of given size
 */
t_iemnet_chunk*iemnet__chunk_create_empty(int);
/**
 * initialize a "chunk" (allocate memory,...) with given data
 * receiver address will be set to 0
 *
 * \param size of data
 * \param data of size
 * \return a new chunk that holds a copy of data
 */
t_iemnet_chunk*iemnet__chunk_create_data(int size, unsigned char*data);
/**
 * initialize a "chunk" (allocate memory,...) with given data from specified address
 * \param size of data
 * \param data of size
 * \param addr originating address (can be NULL)
 * \return a new chunk that holds a copy of data
 */
t_iemnet_chunk*iemnet__chunk_create_dataaddr(int size, unsigned char*data, struct sockaddr_in*addr);
/**
 * initialize a "chunk" (allocate memory,...) with given data
 * receiver address will be set to 0
 *
 * \param argc size of list
 * \param argv list of atoms containing only "bytes" (t_floats [0..255])
 * \return a new chunk that holds a copy of the list data
 */
t_iemnet_chunk*iemnet__chunk_create_list(int argc, t_atom*argv);
/**
 * initialize a "chunk" (allocate memory,...) from another chunk
 *
 * \param src the source chunk
 * \return a new chunk that holds a copy of the source data
 */
t_iemnet_chunk*iemnet__chunk_create_chunk(t_iemnet_chunk*source);


/* iemnet_sender.c */

/**
 * opaque data type used for sending data over a socket
 */
typedef struct _iemnet_sender t_iemnet_sender;
EXTERN_STRUCT _iemnet_sender;

/**
 * create a sender to a given socket
 *
 * \param sock a previously opened socket
 * \return pointer to a sender object
 * \note the socket must be writeable
 */
t_iemnet_sender*iemnet__sender_create(int sock);
/**
 * destroy a sender to a given socket
 * destroying a sender will free all resources of the sender
 *
 * \param pointer to a sender object to be destroyed
 *
 * \note  it will also close() the socket
 */
void iemnet__sender_destroy(t_iemnet_sender*);

/**
 * send data over a socket
 *
 * \param pointer to a sender object 
 * \param pointer to a chunk of data to be sent
 * \return the current fill state of the send buffer
 *
 * \note the sender creates a local copy of chunk; the caller has to delete their own copy
 */
int iemnet__sender_send(t_iemnet_sender*, t_iemnet_chunk*);

/**
 * query the fill state of the send buffer
 *
 * \param pointer to a sender object 
 * \return the current fill state of the send buffer
 */
int iemnet__sender_getsize(t_iemnet_sender*);


/* iemnet_receiver.c */

/**
 * opaque data type used for receiving data from a socket
 */
typedef struct _iemnet_receiver t_iemnet_receiver;
EXTERN_STRUCT _iemnet_receiver;
/**
 * callback function for receiving
 * whenever data arrives at the socket, a callback will be called synchronously
 */
typedef void (*t_iemnet_receivecallback)(void*userdata, 
					 t_iemnet_chunk*rawdata,
					 int argc, t_atom*argv);

/**
 * create a receiver object
 *
 *  whenever something is received on the socket, the callback is called with the payload in the main thread of the caller
 *
 * \param sock the (readable) socket to receive from
 * \param data user data to be passed to callback
 * \param callback a callback function that is called on the caller's side
 *
 * \note the callback will be scheduled in the caller's thread with clock_delay()
 */
t_iemnet_receiver*iemnet__receiver_create(int sock, void*data, t_iemnet_receivecallback callback);
/**
 * destroy a receiver at a given socket
 * destroying a receiver will free all resources of the receiver
 *
 * \param pointer to a receiver object to be destroyed
 *
 * \note  it will also close() the socket
 */
void iemnet__receiver_destroy(t_iemnet_receiver*);

/**
 * query the fill state of the receive buffer
 *
 * \param pointer to a receiver object 
 * \return the current fill state of the receive buffer
 */
int iemnet__receiver_getsize(t_iemnet_receiver*);



/* convenience functions */

/**
 * output the address  (IP, port)
 * the given address is first output through the status_outlet as a "host" message
 * and then as a list through the address_outlet
 *
 * \param status_outlet outlet for general status messages
 * \param address_outlet outlet for addresses only
 * \param address the host ip
 * \param port the host port
 *
 * \note the address will be output as a 5 element list, with the 1st 4 elements denoting the quads of the IP address (as bytes) and the last element the port
 */
void iemnet__addrout(t_outlet*status_outlet, t_outlet*address_outlet, long address, unsigned short port);

/**
 * output the socket we received data from
 * the given socket is first output through the status_outlet as a "socket" message
 * and then as a single number through the socket_outlet
 *
 * \param status_outlet outlet for general status messages
 * \param socket_outlet outlet for sockets only
 * \param sockfd the socket
 */
void iemnet__socketout(t_outlet*status_outlet, t_outlet*socket_outlet, int sockfd);

/**
 * output the number of connections
 * the given number of connections is first output through the status_outlet as a "connections" message
 * and then as a single number through the numconn_outlet
 *
 * \param status_outlet outlet for general status messages
 * \param address_outlet outlet for numconnections only
 * \param numconnections the number of connections
 */
void iemnet__numconnout(t_outlet*status_outlet, t_outlet*numconn_outlet, int numconnections);

/**
 * output a list as a stream (serialize)
 *
 * the given list of atoms will be sent to the output one-by-one
 *
 * \param outlet outlet to sent the data to
 * \param argc size of the list
 * \param argv data
 *
 * \note with stream based protocols (TCP/IP) the length of the received lists has no meaning, so the data has to be serialized anyhow
 */
void iemnet__streamout(t_outlet*outlet, int argc, t_atom*argv);

/**
 * register an objectname and printout a banner
 *
 * this will printout a copyright notice
 * additionally, it will return whether it has already been called for the given name
 *
 * \param name an objectname to "register"
 * \return 1 if this function has been called the first time with the given name, 0 otherwise
 *
 */
int iemnet__register(const char*name);


#if defined(_MSC_VER)
# define snprintf _snprintf
# define IEMNET_EXTERN __declspec(dllexport) extern
# define CCALL __cdecl
# define IEMNET_INITIALIZER(f) \
   static void autoinit__ ## f(void) { f(); }
#elif defined(__GNUC__)
# define IEMNET_EXTERN extern
# define CCALL
# define IEMNET_INITIALIZER(f) \
  static void autoinit__ ## f(void) __attribute__((constructor)); \
  static void autoinit__ ## f(void) { f(); }
#endif


/**
 * \fn void DEBUG(const char* format,...);
 *
 * \brief debug output
 * \note this will only take effect if DEBUG is not undefined
 */
#ifdef DEBUG
# undef DEBUG
# define DEBUG startpost("[%s:%d]", __FUNCTION__, __LINE__); post
#else
static void debug_dummy(const char *format, ...)  {;}
# define DEBUG debug_dummy
#endif


#endif /* INCLUDE_IEMNET_H_ */
