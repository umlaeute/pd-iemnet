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
/* along with this program; if not, see                                         */
/*     http://www.gnu.org/licenses/                                             */
/*                                                                              */

/* ---------------------------------------------------------------------------- */

#ifndef INCLUDE_IEMNET_H_
#define INCLUDE_IEMNET_H_

#ifdef __GNUC__
#  define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#  define MAYBE_UNUSED(x) x __attribute__((__unused__))
#else
#  define UNUSED(x) UNUSED_ ## x
#  define MAYBE_UNUSED(x) x
#endif

#ifdef __GNUC__
#  define UNUSED_FUNCTION(x) __attribute__((__unused__)) UNUSED_ ## x
#  define MAYBE_UNUSED_FUNCTION(x) __attribute__((__unused__)) x
#else
#  define UNUSED_FUNCTION(x) UNUSED_ ## x
#  define MAYBE_UNUSED_FUNCTION(x) x
#endif

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
#endif
#include <sys/types.h>

/* iemnet_data.c */
#include "iemnet_data.h"


/* iemnet_sender.c */

/**
 * opaque data type used for sending data over a socket
 */
typedef struct _iemnet_sender t_iemnet_sender;
EXTERN_STRUCT _iemnet_sender;

/**
 * user provided send function
 * (defaults to just using send)
 * this function is guaranteed to be called with a valid 'chunk',
 * and the 'userdata' and 'sockfd' provided at sender-creation
 */
typedef int (*t_iemnet_sendfunction)(const void*userdata, int sockfd,
                                     t_iemnet_chunk*chunk);

/**
 * create a sender to a given socket
 *
 * \param sock a previously opened socket
 * \param sendfun a send()-implementation (or NULL, to use the default send/sendto based implementation)
 * \param userdata pointer to optional userdata to be passsed to `sendfun`
 * \param bool indicating whether this function is called from a subthread (1) or the mainthread (0)
 * \return pointer to a sender object
 * \note the socket must be writeable
 */
t_iemnet_sender*iemnet__sender_create(int sock,
                                      t_iemnet_sendfunction sendfun, const void*userdata,
                                      int);
/**
 * destroy a sender to a given socket
 * destroying a sender will free all resources of the sender
 *
 * \param pointer to a sender object to be destroyed
 * \param bool indicating whether this function is called from a subthread (1) or the mainthread (0)
 *
 * \note  it will also close() the socket
 */
void iemnet__sender_destroy(t_iemnet_sender*, int);

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


/**
 * calls connect(2) with a timeout.
 * if "timeout" is <0, then the original blocking behaviour is preserved.
 *
 * \param timeout timeout in ms
 * \return success
 */
int iemnet__connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen, float timeout);


/* iemnet_receiver.c */

/**
 * opaque data type used for receiving data from a socket
 */
typedef struct _iemnet_receiver t_iemnet_receiver;
EXTERN_STRUCT _iemnet_receiver;
/**
 * callback function for receiving
 * whenever data arrives at the socket, a callback will be called synchronously
 * if rawdata is NULL, this signifies that the socket has been closed
 */
typedef void (*t_iemnet_receivecallback)(void*userdata
    , t_iemnet_chunk*rawdata
                                        );

/**
 * create a receiver object
 *
 *  whenever something is received on the socket, the callback is called with the payload in the main thread of the caller
 *
 * \param sock the (readable) socket to receive from
 * \param data user data to be passed to callback
 * \param callback a callback function that is called on the caller's side
 * \param subthread bool indicating whether this function is called from a subthread (1) or the mainthread (0)
 *
 * \note the callback will be scheduled in the caller's thread with clock_delay()
 */
t_iemnet_receiver*iemnet__receiver_create(int sock, void*data,
    t_iemnet_receivecallback callback, int subthread);
/**
 * destroy a receiver at a given socket
 * destroying a receiver will free all resources of the receiver
 *
 * \param pointer to a receiver object to be destroyed
 * \param bool indicating whether this function is called from a subthread (1) or the mainthread (0)
 *
 * \note  it will also close() the socket
 */
void iemnet__receiver_destroy(t_iemnet_receiver*, int subthread);

/**
 * query the fill state of the receive buffer
 *
 * \param pointer to a receiver object
 * \return the current fill state of the receive buffer
 */
int iemnet__receiver_getsize(t_iemnet_receiver*);


/* convenience functions */

/**
 * properly close a socket fd
 *
 * \param sock socket to close
 */
void iemnet__closesocket(int fd, int verbose);

/**
 * convert a sockaddr to an atom-list, that can be output
 *
 * \param address a pointer to sockaddr_in/sockaddr_in6/... that holds the address
 * \param alist an array of at least 18 t_atoms to write the address to in the form:
 *              <s:family> {<f:IPitem>} {f:port}
 *
 * \return the number of atoms consumed
 */
int iemnet__sockaddr2list(const struct sockaddr_storage*address, t_atom alist[18]);

/**
 * output the address  (IP, port)
 * the address is obtained from the sockfd via getsockname().
 * the given address is output through the status_outlet using the
 * provided selector, using the form:
 *       '<selector> <family> [<address-component>] <port>'
 *  e.g. 'local_address IPv4 127 0 0 1 65432'
 * resp. 'local_address IPv6 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 55555'
 *
 * \param sockfd the socket to read the address from
 * \param status_outlet outlet for general status messages
 * \param selector the selector to use when sending the status-message
 *
 * \note depending on the address-family the output can have different length.
 *       e.g. with IPv4 the address will be output as a 6 element list, with
 *            the 1st 4 elements denoting the quads of the IPv4 address (as
 *            bytes) and the last element the port.
 */
void iemnet__socket2addressout(int sockfd,
                               t_outlet*status_outlet, t_symbol*selector);

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
void iemnet__addrout(t_outlet*status_outlet, t_outlet*address_outlet,
                     uint32_t address, uint16_t port);

/**
 * output the socket we received data from
 * the given socket is first output through the status_outlet as a "socket" message
 * and then as a single number through the socket_outlet
 *
 * \param status_outlet outlet for general status messages
 * \param socket_outlet outlet for sockets only
 * \param sockfd the socket
 */
void iemnet__socketout(t_outlet*status_outlet, t_outlet*socket_outlet,
                       int sockfd);

/**
 * output the number of connections
 * the given number of connections is first output through the status_outlet as a "connections" message
 * and then as a single number through the numconn_outlet
 *
 * \param status_outlet outlet for general status messages
 * \param address_outlet outlet for numconnections only
 * \param numconnections the number of connections
 */
void iemnet__numconnout(t_outlet*status_outlet, t_outlet*numconn_outlet,
                        int numconnections);

/**
 * output a list as a stream (serialize)
 *
 * the given list of atoms will be sent to the output one-by-one
 *
 * \param outlet outlet to sent the data to
 * \param argc size of the list
 * \param argv data
 * \param stream if true, serialize the data; if false output as "packets"
 *
 * \note with stream based protocols (TCP/IP) the length of the received lists has no meaning, so the data has to be serialized anyhow; however when creating proxies, sending serialized data is often slow, so there is an option to disable serialization
 */
void iemnet__streamout(t_outlet*outlet, int argc, t_atom*argv, int stream);

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
# define IEMNET_INITIALIZER(f)
#elif defined(__GNUC__)
# define IEMNET_EXTERN extern
# define IEMNET_INITIALIZER(f)
#endif

typedef enum {
  IEMNET_FATAL   = 0,
  IEMNET_ERROR   = 1,
  IEMNET_NORMAL  = 2,
  IEMNET_VERBOSE = 3,
  IEMNET_DEBUG   = 4
} t_iemnet_loglevel;
void iemnet_log(const void *object, const t_iemnet_loglevel level, const char *fmt, ...);
/**
 * \fn void DEBUG(const char* format,...);
 *
 * \brief debug output
 * \note this will only take effect if DEBUG is not undefined
 */
#ifdef IEMNET_HAVE_DEBUG
# undef IEMNET_HAVE_DEBUG
#endif

#ifdef DEBUG
# define IEMNET_HAVE_DEBUG 1
#endif

#define IEMNET_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + ((c) > 255 ? 255 : (c)))

void iemnet_debuglevel(void*,t_float);
int iemnet_debug(int debuglevel, const char*file, unsigned int line,
                 const char*function);
#define DEBUGMETHOD(c) class_addmethod(c, (t_method)iemnet_debuglevel, gensym("debug"), A_FLOAT, 0)



#ifdef DEBUG
# undef DEBUG
# define DEBUG if(iemnet_debug(DEBUGLEVEL, __FILE__, __LINE__, __FUNCTION__))post
#else
static void MAYBE_UNUSED_FUNCTION(debug_dummy)(const char *format, ...)
{
  (void)format; /* ignore unused variable */
}
# define DEBUG if(0)debug_dummy
#endif
#define MARK() post("%s:%d [%s]",  __FILE__, __LINE__, __FUNCTION__)


#endif /* INCLUDE_IEMNET_H_ */
