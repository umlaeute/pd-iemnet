/* x_net_tcpserver.c Martin Peach 20060511 working version 20060512 */
/* 20060515 works on linux too... */
/* x_net_tcpserver.c is based on netserver: */
/* --------------------------  netserver  ------------------------------------- */
/*                                                                              */
/* A server for bidirectional communication from within Pd.                     */
/* Allows to send back data to specific clients connected to the server.        */
/* Written by Olaf Matthes <olaf.matthes@gmx.de>                                */
/* Get source at http://www.akustische-kunst.org/puredata/maxlib                */
/*                                                                              */
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
/* Based on PureData by Miller Puckette and others.                             */
/*                                                                              */
/* ---------------------------------------------------------------------------- */
//define DEBUG

#include "m_pd.h"
#include "m_imp.h"
#include "s_stuff.h"

//#include <sys/types.h>
//#include <stdarg.h>
//#include <signal.h>
//#include <fcntl.h>
//#include <errno.h>
//#include <string.h>
//#include <stdio.h>
//#include <pthread.h>
#if defined(UNIX) || defined(unix)
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#define SOCKET_ERROR -1
#else
//#include <io.h>
//#include <fcntl.h>
#include <winsock2.h>
#endif

#define MAX_CONNECT 32 /* maximum number of connections */
#define INBUFSIZE 65536L /* was 4096: size of receiving data buffer */
#define MAX_UDP_RECEIVE 65536L /* longer than data in maximum UDP packet */

/* ----------------------------- tcpserver ------------------------- */

static t_class *tcpserver_class;
static t_binbuf *inbinbuf;
static char objName[] = "tcpserver";

typedef void (*t_tcpserver_socketnotifier)(void *x);
typedef void (*t_tcpserver_socketreceivefn)(void *x, t_binbuf *b);

typedef struct _tcpserver_socketreceiver
{
    unsigned char               *sr_inbuf;
    int                         sr_inhead;
    int                         sr_intail;
    void                        *sr_owner;
    t_tcpserver_socketnotifier  sr_notifier;
    t_tcpserver_socketreceivefn sr_socketreceivefn;
} t_tcpserver_socketreceiver;

typedef struct _tcpserver
{
    t_object                    x_obj;
    t_outlet                    *x_msgout;
    t_outlet                    *x_connectout;
    t_outlet                    *x_sockout;
    t_outlet                    *x_addrout;
    t_symbol                    *x_host[MAX_CONNECT];
    t_int                       x_fd[MAX_CONNECT];
    u_long                      x_addr[MAX_CONNECT];
    t_tcpserver_socketreceiver  *x_sr[MAX_CONNECT];
    t_atom                      x_addrbytes[4];
    t_int                       x_sock_fd;
    t_int                       x_connectsocket;
    t_int                       x_nconnections;
    t_atom                      x_msgoutbuf[MAX_UDP_RECEIVE];
    char                        x_msginbuf[MAX_UDP_RECEIVE];
} t_tcpserver;

static t_tcpserver_socketreceiver *tcpserver_socketreceiver_new(void *owner, t_tcpserver_socketnotifier notifier,
    t_tcpserver_socketreceivefn socketreceivefn);
static int tcpserver_socketreceiver_doread(t_tcpserver_socketreceiver *x);
static void tcpserver_socketreceiver_read(t_tcpserver_socketreceiver *x, int fd);
static void tcpserver_socketreceiver_free(t_tcpserver_socketreceiver *x);
static void tcpserver_send(t_tcpserver *x, t_symbol *s, int argc, t_atom *argv);
static void tcp_server_send_bytes(int sockfd, t_tcpserver *x, int argc, t_atom *argv);
static void tcpserver_client_send(t_tcpserver *x, t_symbol *s, int argc, t_atom *argv);
static void tcpserver_broadcast(t_tcpserver *x, t_symbol *s, int argc, t_atom *argv);
static void tcpserver_notify(t_tcpserver *x);
static void tcpserver_connectpoll(t_tcpserver *x);
static void tcpserver_print(t_tcpserver *x);
static void *tcpserver_new(t_floatarg fportno);
static void tcpserver_free(t_tcpserver *x);
#ifdef MSW
__declspec(dllexport)
#endif
void tcpserver_setup(void);

static t_tcpserver_socketreceiver *tcpserver_socketreceiver_new(void *owner, t_tcpserver_socketnotifier notifier,
    t_tcpserver_socketreceivefn socketreceivefn)
{
    t_tcpserver_socketreceiver *x = (t_tcpserver_socketreceiver *)getbytes(sizeof(*x));
    if (!x)
    {
        error("%s_socketreceiver: unable to allocate %d bytes", objName, sizeof(*x));
    }
    else
    {
        x->sr_inhead = x->sr_intail = 0;
        x->sr_owner = owner;
        x->sr_notifier = notifier;
        x->sr_socketreceivefn = socketreceivefn;
        if (!(x->sr_inbuf = malloc(INBUFSIZE)))
        {
            freebytes(x, sizeof(*x));
            x = NULL;
            error("%s_socketreceiver: unable to allocate %d bytes", objName, INBUFSIZE);
        }
    }
    return (x);
}

/* this is in a separately called subroutine so that the buffer isn't
   sitting on the stack while the messages are getting passed. */
static int tcpserver_socketreceiver_doread(t_tcpserver_socketreceiver *x)
{
    char            messbuf[INBUFSIZE];
    char            *bp = messbuf;
    int             indx, i;
    int             inhead = x->sr_inhead;
    int             intail = x->sr_intail;
    unsigned char   c;
    t_tcpserver     *y = x->sr_owner;
    unsigned char   *inbuf = x->sr_inbuf;

    if (intail == inhead) return (0);
#ifdef DEBUG
    post ("%s_socketreceiver_doread: intail=%d inhead=%d", objName, intail, inhead);
#endif

    for (indx = intail, i = 0; indx != inhead; indx = (indx+1)&(INBUFSIZE-1), ++i)
    {
        c = *bp++ = inbuf[indx];
        y->x_msgoutbuf[i].a_w.w_float = (float)c;
    }
    if (i > 1) outlet_list(y->x_msgout, &s_list, i, y->x_msgoutbuf);
    else outlet_float(y->x_msgout, y->x_msgoutbuf[0].a_w.w_float);

 //   intail = (indx+1)&(INBUFSIZE-1);
    x->sr_inhead = inhead;
    x->sr_intail = indx;//intail;
    return (1);
}

static void tcpserver_socketreceiver_read(t_tcpserver_socketreceiver *x, int fd)
{
    int         readto = (x->sr_inhead >= x->sr_intail ? INBUFSIZE : x->sr_intail-1);
    int         ret, i;
    t_tcpserver *y = x->sr_owner;

    y->x_sock_fd = fd;
    /* the input buffer might be full.  If so, drop the whole thing */
    if (readto == x->sr_inhead)
    {
        post("%s: dropped message", objName);
        x->sr_inhead = x->sr_intail = 0;
        readto = INBUFSIZE;
    }
    else
    {
        ret = recv(fd, x->sr_inbuf + x->sr_inhead,
            readto - x->sr_inhead, 0);
        if (ret < 0)
        {
            sys_sockerror("tcpserver: recv");
            if (x->sr_notifier) (*x->sr_notifier)(x->sr_owner);
            sys_rmpollfn(fd);
            sys_closesocket(fd);
        }
        else if (ret == 0)
        {
            post("%s: connection closed on socket %d", objName, fd);
            if (x->sr_notifier) (*x->sr_notifier)(x->sr_owner);
            sys_rmpollfn(fd);
            sys_closesocket(fd);
        }
        else
        {
#ifdef DEBUG
    post ("%s_socketreceiver_read: ret = %d", objName, ret);
#endif
            x->sr_inhead += ret;
            if (x->sr_inhead >= INBUFSIZE) x->sr_inhead = 0;
            /* output client's IP and socket no. */
            for(i = 0; i < y->x_nconnections; i++)	/* search for corresponding IP */
            {
                if(y->x_fd[i] == y->x_sock_fd)
                {
//                  outlet_symbol(x->x_connectionip, x->x_host[i]);
                    /* find sender's ip address and output it */
                    y->x_addrbytes[0].a_w.w_float = (y->x_addr[i] & 0xFF000000)>>24;
                    y->x_addrbytes[1].a_w.w_float = (y->x_addr[i] & 0x0FF0000)>>16;
                    y->x_addrbytes[2].a_w.w_float = (y->x_addr[i] & 0x0FF00)>>8;
                    y->x_addrbytes[3].a_w.w_float = (y->x_addr[i] & 0x0FF);
                    outlet_list(y->x_addrout, &s_list, 4L, y->x_addrbytes);
                    break;
                }
            }
            outlet_float(y->x_sockout, y->x_sock_fd);	/* the socket number */
            tcpserver_socketreceiver_doread(x);
        }
    }
}

static void tcpserver_socketreceiver_free(t_tcpserver_socketreceiver *x)
{
    if (x != NULL)
    {
        free(x->sr_inbuf);
        freebytes(x, sizeof(*x));
    }
}

/* ---------------- main tcpserver (send) stuff --------------------- */

static void tcp_server_send_bytes(int client, t_tcpserver *x, int argc, t_atom *argv)
{
    static char     byte_buf[MAX_UDP_RECEIVE];// arbitrary maximum similar to max IP packet size
    int             i, d;
    unsigned char   c;
    float           f, e;
    char            *bp;
    int             length, sent;
    int             result;
    static double   lastwarntime;
    static double   pleasewarn;
    double          timebefore;
    double          timeafter;
    int             late;
    int             sockfd = x->x_fd[client];

    /* process & send data */
    if(sockfd >= 0)
    {
        for (i = 0; i < argc; ++i)
        {
            if (argv[i].a_type == A_FLOAT)
            {
                f = argv[i].a_w.w_float;
                d = (int)f;
                e = f - d;
#ifdef DEBUG
                post("%s: argv[%d]: float:%f int:%d delta:%f", objName, i, f, d, e);
#endif
                if (e != 0)
                {
                    error("%s: item %d (%f) is not an integer", objName, i, f);
                    return;
                }
                if ((d < 0) || (d > 255))
                {
                    error("%s: item %d (%f) is not between 0 and 255", objName, i, f);
                    return;
                }
                c = (unsigned char)d; /* make sure it doesn't become negative; this only matters for post() */
#ifdef DEBUG
                post("%s: argv[%d]: %d", objName, i, c);
#endif
                byte_buf[i] = c;
            }
            else
            {
                error("%s: item %d is not a float", objName, i);
                return;
            }
        }
        length = i;
        if (length > 0)
        {
            for (bp = byte_buf, sent = 0; sent < length;)
            {
                timebefore = sys_getrealtime();
                result = send(sockfd, byte_buf, length-sent, 0);
                timeafter = sys_getrealtime();
                late = (timeafter - timebefore > 0.005);
                if (late || pleasewarn)
                {
                    if (timeafter > lastwarntime + 2)
                    {
                        post("%s: send blocked %d msec", objName,
                            (int)(1000 * ((timeafter - timebefore) + pleasewarn)));
                        pleasewarn = 0;
                        lastwarntime = timeafter;
                    }
                    else if (late) pleasewarn += timeafter - timebefore;
                }
                if (result <= 0)
                {
                    sys_sockerror("tcpserver: send");
                    post("%s: could not send data to client %d", objName, client);
                    break;
                }
                else
                {
                    sent += result;
                    bp += result;
                }
            }
        }
    }
    else post("%s: not a valid socket number (%d)", objName, sockfd);
}

/* send message to client using socket number */
static void tcpserver_send(t_tcpserver *x, t_symbol *s, int argc, t_atom *argv)
{
    int     i, sockfd;
    int     client = -1;

    if(x->x_nconnections < 0)
    {
        post("%s: no clients connected", objName);
        return;
    }
    if(argc < 2)
    {
        post("%s: nothing to send", objName);
        return;
    }
    /* get socket number of connection (first element in list) */
    if(argv[0].a_type == A_FLOAT)
    {
        sockfd = atom_getfloatarg(0, argc, argv);
        for(i = 0; i < x->x_nconnections; i++)	/* check if connection exists */
        {
            if(x->x_fd[i] == sockfd)
            {
                client = i;	/* the client we're sending to */
                break;
            }
        }
        if(client == -1)
        {
            post("%s: no connection on socket %d", objName, sockfd);
            return;
        }
    }
    else
    {
        post("%s: no socket specified", objName);
        return;
    }
    tcp_server_send_bytes(client, x, argc-1, &argv[1]);
}

/* send message to client using client number
   note that the client numbers might change in case a client disconnects! */
/* clients start at 1 but our index starts at 0 */
static void tcpserver_client_send(t_tcpserver *x, t_symbol *s, int argc, t_atom *argv)
{
    int     client;

    if(x->x_nconnections < 0)
    {
        post("%s: no clients connected", objName);
        return;
    }
    if(argc < 2)
    {
        post("%s: nothing to send", objName);
        return;
    }
    /* get number of client (first element in list) */
    if(argv[0].a_type == A_FLOAT)
        client = atom_getfloatarg(0, argc, argv);
    else
    {
        post("%s: no client specified", objName);
        return;
    }
    if (!((client > 0) && (client < MAX_CONNECT)))
    {
        post("%s: client %d out of range [1..%d]", objName, client, MAX_CONNECT);
        return;
    }
    --client;/* zero based index*/
    tcp_server_send_bytes(client, x, argc-1, &argv[1]);
}

/* broadcasts a message to all connected clients */
static void tcpserver_broadcast(t_tcpserver *x, t_symbol *s, int argc, t_atom *argv)
{
    int     client;

    /* enumerate through the clients and send each the message */
    for(client = 0; client < x->x_nconnections; client++)	/* check if connection exists */
    {
        if(x->x_fd[client] >= 0)
        { /* socket exists for this client */
            tcp_server_send_bytes(client, x, argc, argv);
            break;
        }
    }
}

/* ---------------- main tcpserver (receive) stuff --------------------- */

static void tcpserver_notify(t_tcpserver *x)
{
    int     i, k;

    /* remove connection from list */
    for(i = 0; i < x->x_nconnections; i++)
    {
        if(x->x_fd[i] == x->x_sock_fd)
        {
            x->x_nconnections--;
            post("%s: \"%s\" removed from list of clients", objName, x->x_host[i]->s_name);
            x->x_host[i] = NULL;	/* delete entry */
            x->x_fd[i] = -1;
            tcpserver_socketreceiver_free(x->x_sr[i]);
            x->x_sr[i] = NULL;

            /* rearrange list now: move entries to close the gap */
            for(k = i; k < x->x_nconnections; k++)
            {
                x->x_host[k] = x->x_host[k + 1];
                x->x_fd[k] = x->x_fd[k + 1];
            }
        }
    }
    outlet_float(x->x_connectout, x->x_nconnections);
}

static void tcpserver_connectpoll(t_tcpserver *x)
{
    struct sockaddr_in  incomer_address;
    int                 sockaddrl = (int) sizeof( struct sockaddr );
    int                 fd = accept(x->x_connectsocket, (struct sockaddr*)&incomer_address, &sockaddrl);
    int                 i;

    if (fd < 0) post("%s: accept failed", objName);
    else
    {
        t_tcpserver_socketreceiver *y = tcpserver_socketreceiver_new((void *)x,
            (t_tcpserver_socketnotifier)tcpserver_notify, NULL);/* MP tcpserver_doit isn't used I think...*/
        if (!y)
        {
#ifdef MSW
            closesocket(fd);
#else
            close(fd);
#endif
            return;
        }
        sys_addpollfn(fd, (t_fdpollfn)tcpserver_socketreceiver_read, y);
        x->x_nconnections++;
        i = x->x_nconnections - 1;
        x->x_host[i] = gensym(inet_ntoa(incomer_address.sin_addr));
        x->x_fd[i] = fd;
		x->x_sr[i] = y;
        post("%s: accepted connection from %s on socket %d",
            objName, x->x_host[i]->s_name, x->x_fd[i]);
        outlet_float(x->x_connectout, x->x_nconnections);
        outlet_float(x->x_sockout, x->x_fd[i]);	/* the socket number */
        x->x_addr[i] = ntohl(incomer_address.sin_addr.s_addr);
        x->x_addrbytes[0].a_w.w_float = (x->x_addr[i] & 0xFF000000)>>24;
        x->x_addrbytes[1].a_w.w_float = (x->x_addr[i] & 0x0FF0000)>>16;
        x->x_addrbytes[2].a_w.w_float = (x->x_addr[i] & 0x0FF00)>>8;
        x->x_addrbytes[3].a_w.w_float = (x->x_addr[i] & 0x0FF);
        outlet_list(x->x_addrout, &s_list, 4L, x->x_addrbytes);
    }
}

static void tcpserver_print(t_tcpserver *x)
{
    int     i;

    if(x->x_nconnections > 0)
    {
        post("%s: %d open connections:", objName, x->x_nconnections);
        for(i = 0; i < x->x_nconnections; i++)
        {
            post("        \"%s\" on socket %d",
                x->x_host[i]->s_name, x->x_fd[i]);
        }
    }
    else post("%s: no open connections", objName);
}

static void *tcpserver_new(t_floatarg fportno)
{
    t_tcpserver         *x;
    int                 i;
    struct sockaddr_in  server;
    int                 sockfd, portno = fportno;

    /* create a socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
#ifdef DEBUG
    post("%s: receive socket %d", objName, sockfd);
#endif
    if (sockfd < 0)
    {
        sys_sockerror("tcpserver: socket");
        return (0);
    }
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
#ifdef IRIX
    /* this seems to work only in IRIX but is unnecessary in
        Linux.  Not sure what NT needs in place of this. */
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 0, 0) < 0)
        post("setsockopt failed\n");
#endif
    /* assign server port number */
    server.sin_port = htons((u_short)portno);
    /* name the socket */
    if (bind(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        sys_sockerror("tcpserver: bind");
        sys_closesocket(sockfd);
        return (0);
    }
    x = (t_tcpserver *)pd_new(tcpserver_class);
    x->x_msgout = outlet_new(&x->x_obj, &s_anything); /* 1st outlet for received data */
    /* streaming protocol */
    if (listen(sockfd, 5) < 0)
    {
        sys_sockerror("tcpserver: listen");
        sys_closesocket(sockfd);
        sockfd = -1;
    }
    else
    {
        sys_addpollfn(sockfd, (t_fdpollfn)tcpserver_connectpoll, x);
        x->x_connectout = outlet_new(&x->x_obj, &s_float); /* 2nd outlet for number of connected clients */
        x->x_sockout = outlet_new(&x->x_obj, &s_float); /* 3rd outlet for socket number of current client */
        x->x_addrout = outlet_new(&x->x_obj, &s_list); /* 4th outlet for ip address of current client */
        inbinbuf = binbuf_new();
    }
    x->x_connectsocket = sockfd;
    x->x_nconnections = 0;
    for(i = 0; i < MAX_CONNECT; i++)
    {
        x->x_fd[i] = -1;
        x->x_sr[i] = NULL;
    }
    /* prepare to convert the bytes in the buffer to floats in a list */
    for (i = 0; i < MAX_UDP_RECEIVE; ++i)
    {
        x->x_msgoutbuf[i].a_type = A_FLOAT;
        x->x_msgoutbuf[i].a_w.w_float = 0;
    }
    for (i = 0; i < 4; ++i)
    {
        x->x_addrbytes[i].a_type = A_FLOAT;
        x->x_addrbytes[i].a_w.w_float = 0;
    }

    return (x);
}

static void tcpserver_free(t_tcpserver *x)
{
    int     i;

    for(i = 0; i < MAX_CONNECT; i++)
    {
        if (x->x_fd[i] >= 0)
        {
            sys_rmpollfn(x->x_fd[i]);
            sys_closesocket(x->x_fd[i]);
        }
        if (x->x_sr[i] != NULL) tcpserver_socketreceiver_free(x->x_sr[i]);
    }
    if (x->x_connectsocket >= 0)
    {
        sys_rmpollfn(x->x_connectsocket);
        sys_closesocket(x->x_connectsocket);
    }

    binbuf_free(inbinbuf);
}

#ifdef MSW
__declspec(dllexport)
#endif
void tcpserver_setup(void)
{
    tcpserver_class = class_new(gensym(objName),(t_newmethod)tcpserver_new, (t_method)tcpserver_free,
        sizeof(t_tcpserver), 0, A_DEFFLOAT, 0);
    class_addmethod(tcpserver_class, (t_method)tcpserver_print, gensym("print"), 0);
    class_addmethod(tcpserver_class, (t_method)tcpserver_send, gensym("send"), A_GIMME, 0);
    class_addmethod(tcpserver_class, (t_method)tcpserver_client_send, gensym("client"), A_GIMME, 0);
    class_addmethod(tcpserver_class, (t_method)tcpserver_broadcast, gensym("broadcast"), A_GIMME, 0);
}

/* end of x_net_tcpserver.c */
