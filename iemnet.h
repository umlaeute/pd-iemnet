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
#endif

typedef struct _iemnet_chunk {
  unsigned char* data;

  size_t size;
} t_iemnet_chunk;

void iemnet__chunk_destroy(t_iemnet_chunk*);
t_iemnet_chunk*iemnet__chunk_create_empty(int);
t_iemnet_chunk*iemnet__chunk_create_data(int, unsigned char*);
t_iemnet_chunk*iemnet__chunk_create_list(int, t_atom*);
t_iemnet_chunk*iemnet__chunk_create_chunk(t_iemnet_chunk*);

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

typedef void (*t_iemnet_receivecallback)(void*data, int argc, t_atom*argv);

/**
 * create a receiver object: whenever something is received on the socket,
 * the callback is called with the payload
 */
t_iemnet_receiver*iemnet__receiver_create(int sock, void*data, t_iemnet_receivecallback callback);
void iemnet__receiver_destroy(t_iemnet_receiver*);


#if defined(_MSC_VER)
# define IEMNET_EXTERN __declspec(dllexport) extern
# define CCALL __cdecl
# pragma section(".CRT$XCU",read)
# define IEMNET_INITIALIZER(f) \
   static void __cdecl autoinit__ ## f(void); \
   __declspec(allocate(".CRT$XCU")) void (__cdecl*f##_)(void) = f; \
  static void __cdecl autoinit__ ## f(void) { f(); }
#elif defined(__GNUC__)
# define IEMNET_EXTERN extern
# define CCALL
# define IEMNET_INITIALIZER(f) \
  static void autoinit__ ## f(void) __attribute__((constructor)); \
  static void autoinit__ ## f(void) { f(); }
#endif


#endif /* INCLUDE_IEMNET_H_ */
