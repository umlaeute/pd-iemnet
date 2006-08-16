/* x_net_tcp_client.c Martin Peach 20060508, working version 20060512 */
/* linux version 20060515 */
/* x_net_tcp_client.c is based on netclient: */
/* --------------------------  netclient  ------------------------------------- */
/*                                                                              */
/* Extended 'netsend', connects to 'netserver'.                                 */
/* Uses child thread to connect to server. Thus needs pd0.35-test17 or later.   */
/* Written by Olaf Matthes (olaf.matthes@gmx.de)                                */
/* Get source at http://www.akustische-kunst.org/puredata/maxlib/               */
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
#include "s_stuff.h"

#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>
#if defined(UNIX) || defined(unix)
#include <sys/socket.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#define SOCKET_ERROR -1
#else
#include <winsock2.h>
#endif

#define DEFPOLLTIME 20  /* check for input every 20 ms */

static t_class *tcpclient_class;
static char objName[] = "tcpclient";
#define MAX_UDP_RECEIVE 65536L // longer than data in maximum UDP packet

typedef struct _tcpclient
{
    t_object        x_obj;
    t_clock         *x_clock;
    t_clock         *x_poll;
    t_outlet        *x_msgout;
    t_outlet        *x_addrout;
    t_outlet        *x_outconnect;
    int             x_dump; // 1 = hexdump received bytes
    int             x_fd; // the socket
    char            *x_hostname; // address we want to connect to as text
    int             x_connectstate; // 0 = not connected, 1 = connected
    int             x_port; // port we're connected to
    long            x_addr; // address we're connected to as 32bit int
    t_atom          x_addrbytes[4]; // address we're connected to as 4 bytes
    t_atom          x_msgoutbuf[MAX_UDP_RECEIVE]; // received data as float atoms
    unsigned char   x_msginbuf[MAX_UDP_RECEIVE]; // received data as bytes
    /* multithread stuff */
    pthread_t       x_threadid; /* id of child thread */
    pthread_attr_t  x_threadattr; /* attributes of child thread */
} t_tcpclient;

static void tcpclient_dump(t_tcpclient *x, t_float dump);
static void tcp_client_hexdump(unsigned char *buf, long len);
static void tcpclient_tick(t_tcpclient *x);
static void *tcpclient_child_connect(void *w);
static void tcpclient_connect(t_tcpclient *x, t_symbol *hostname, t_floatarg fportno);
static void tcpclient_disconnect(t_tcpclient *x);
static void tcpclient_send(t_tcpclient *x, t_symbol *s, int argc, t_atom *argv);
static void tcpclient_rcv(t_tcpclient *x);
static void tcpclient_poll(t_tcpclient *x);
static void *tcpclient_new(t_floatarg udpflag);
static void tcpclient_free(t_tcpclient *x);
#ifdef MSW
__declspec(dllexport)
#endif
void tcpclient_setup(void);

static void tcpclient_dump(t_tcpclient *x, t_float dump)
{
    x->x_dump = (dump == 0)?0:1;
}

static void tcp_client_hexdump(unsigned char *buf, long len)
{
#define BYTES_PER_LINE 16
    char hexStr[(3*BYTES_PER_LINE)+1];
    char ascStr[BYTES_PER_LINE+1];
    long i, j, k = 0L;
#ifdef DEBUG
    post("tcp_client_hexdump %d", len);
#endif
    while (k < len)
    {
        for (i = j = 0; i < BYTES_PER_LINE; ++i, ++k, j+=3)
        {
            if (k < len)
            {
#ifdef MSW
                sprintf_s(&hexStr[j], 4, "%02X ", buf[k]);
                sprintf_s(&ascStr[i], 2, "%c", ((buf[k] >= 32) && (buf[k] <= 126))? buf[k]: '.');
#else
                snprintf(&hexStr[j], 4, "%02X ", buf[k]);
                snprintf(&ascStr[i], 2, "%c", ((buf[k] >= 32) && (buf[k] <= 126))? buf[k]: '.');
#endif
            }
            else
            { // the last line
#ifdef MSW
                sprintf_s(&hexStr[j], 4, "   ");
                sprintf_s(&ascStr[i], 2, " ");
#else
                snprintf(&hexStr[j], 4, "   ");
                snprintf(&ascStr[i], 2, " ");
#endif
            }
        }
        post ("%s%s", hexStr, ascStr);
    }
}

static void tcpclient_tick(t_tcpclient *x)
{
    outlet_float(x->x_outconnect, 1);
}

static void *tcpclient_child_connect(void *w)
{
    t_tcpclient         *x = (t_tcpclient*) w;
    struct sockaddr_in  server;
    struct hostent      *hp;
    int                 sockfd;

    if (x->x_fd >= 0)
    {
        error("%s_connect: already connected", objName);
        return (x);
    }

    /* create a socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
#ifdef DEBUG
    post("%s: send socket %d\n", objName, sockfd);
#endif
    if (sockfd < 0)
    {
        sys_sockerror("tcpclient: socket");
        return (x);
    }
    /* connect socket using hostname provided in command line */
    server.sin_family = AF_INET;
    hp = gethostbyname(x->x_hostname);
    if (hp == 0)
    {
        sys_sockerror("tcpclient: bad host?\n");
        return (x);
    }
    memcpy((char *)&server.sin_addr, (char *)hp->h_addr, hp->h_length);

    /* assign client port number */
    server.sin_port = htons((u_short)x->x_port);

    post("%s: connecting socket %d to port %d", objName, sockfd, x->x_port);
    /* try to connect */
    if (connect(sockfd, (struct sockaddr *) &server, sizeof (server)) < 0)
    {
        sys_sockerror("tcpclient: connecting stream socket");
        sys_closesocket(sockfd);
        return (x);
    }
    x->x_fd = sockfd;
    x->x_addr = ntohl(*(long *)hp->h_addr);
    /* outlet_float is not threadsafe ! */
    // outlet_float(x->x_obj.ob_outlet, 1);
    x->x_connectstate = 1;
    /* use callback instead to set outlet */
    clock_delay(x->x_clock, 0);
    return (x);
}

static void tcpclient_connect(t_tcpclient *x, t_symbol *hostname, t_floatarg fportno)
{
    /* we get hostname and port and pass them on
       to the child thread that establishes the connection */
    x->x_hostname = hostname->s_name;
    x->x_port = fportno;
    x->x_connectstate = 0;
    /* start child thread */
    if(pthread_create(&x->x_threadid, &x->x_threadattr, tcpclient_child_connect, x) < 0)
        post("%s: could not create new thread", objName);
}

static void tcpclient_disconnect(t_tcpclient *x)
{
    if (x->x_fd >= 0)
    {
        sys_closesocket(x->x_fd);
        x->x_fd = -1;
        x->x_connectstate = 0;
        outlet_float(x->x_outconnect, 0);
        post("%s: disconnected", objName);
    }
    else post("%s: not connected", objName);
}

static void tcpclient_send(t_tcpclient *x, t_symbol *s, int argc, t_atom *argv)
{
    static char    byte_buf[65536];// arbitrary maximum similar to max IP packet size
    int            i, d;
    unsigned char  c;
    float          f, e;
    char           *bp;
    int            length, sent;
    int            result;
    static double  lastwarntime;
    static double  pleasewarn;
    double         timebefore;
    double         timeafter;
    int            late;

#ifdef DEBUG
    post("s: %s", s->s_name);
    post("argc: %d", argc);
#endif

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
                error("%s_send: item %d (%f) is not an integer", objName, i, f);
                return;
            }
            if ((d < 0) || (d > 255))
            {
                error("%s: item %d (%f) is not between 0 and 255", objName, i, f);
                return;
            }
            c = (unsigned char)d;
#ifdef DEBUG
            post("%s_send: argv[%d]: %d", objName, i, c);
#endif
            byte_buf[i] = c;
        }
        else
	    {
            error("%s_send: item %d is not a float", objName, i);
            return;
        }
    }

    length = i;
    if ((x->x_fd >= 0) && (length > 0))
    {
        for (bp = byte_buf, sent = 0; sent < length;)
        {
            timebefore = sys_getrealtime();
            result = send(x->x_fd, byte_buf, length-sent, 0);
            timeafter = sys_getrealtime();
            late = (timeafter - timebefore > 0.005);
            if (late || pleasewarn)
            {
                if (timeafter > lastwarntime + 2)
                {
                    post("%s_send blocked %d msec", objName,
                        (int)(1000 * ((timeafter - timebefore) + pleasewarn)));
                    pleasewarn = 0;
                    lastwarntime = timeafter;
                }
                else if (late) pleasewarn += timeafter - timebefore;
            }
            if (result <= 0)
            {
                sys_sockerror("tcpclient_send");
                tcpclient_disconnect(x);
                break;
            }
            else
            {
                sent += result;
                bp += result;
            }
        }
    }
    else error("%s: not connected", objName);
}

static void tcpclient_rcv(t_tcpclient *x)
{
    int             sockfd = x->x_fd;
    int             ret;
    int             i;
    fd_set          readset;
    fd_set          exceptset;
    struct timeval  ztout;

    if(x->x_connectstate)
    {
        /* check if we can read/write from/to the socket */
        FD_ZERO(&readset);
        FD_ZERO(&exceptset);
        FD_SET(x->x_fd, &readset );
        FD_SET(x->x_fd, &exceptset );

        ztout.tv_sec = 0;
        ztout.tv_usec = 0;

        ret = select(sockfd+1, &readset, NULL, &exceptset, &ztout);
        if(ret < 0)
        {
            error("%s: unable to read from socket", objName);
            sys_closesocket(sockfd);
            return;
        }
        if(FD_ISSET(sockfd, &readset) || FD_ISSET(sockfd, &exceptset))
        {
            /* read from server */
            ret = recv(sockfd, x->x_msginbuf, MAX_UDP_RECEIVE, 0);
            if(ret > 0)
            {
#ifdef DEBUG
                x->x_msginbuf[ret] = 0;
                post("%s: received %d bytes ", objName, ret);
#endif
                if (x->x_dump)tcp_client_hexdump(x->x_msginbuf, ret);
                for (i = 0; i < ret; ++i)
                {
                    /* convert the bytes in the buffer to floats in a list */
                    x->x_msgoutbuf[i].a_w.w_float = (float)x->x_msginbuf[i];
                }
                /* find sender's ip address and output it */
                x->x_addrbytes[0].a_w.w_float = (x->x_addr & 0xFF000000)>>24;
                x->x_addrbytes[1].a_w.w_float = (x->x_addr & 0x0FF0000)>>16;
                x->x_addrbytes[2].a_w.w_float = (x->x_addr & 0x0FF00)>>8;
                x->x_addrbytes[3].a_w.w_float = (x->x_addr & 0x0FF);
                outlet_list(x->x_addrout, &s_list, 4L, x->x_addrbytes);
                /* send the list out the outlet */
                if (ret > 1) outlet_list(x->x_msgout, &s_list, ret, x->x_msgoutbuf);
                else outlet_float(x->x_msgout, x->x_msgoutbuf[0].a_w.w_float);
            }
            else
            {
                if (ret < 0)
                {
                    sys_sockerror("tcpclient: recv");
                    tcpclient_disconnect(x);
                }
                else
                {
                    post("%s: connection closed for socket %d\n", objName, sockfd);
                    tcpclient_disconnect(x);
                }
            }
        }
    }
    else post("%s: not connected", objName);
}

static void tcpclient_poll(t_tcpclient *x)
{
    if(x->x_connectstate)
        tcpclient_rcv(x);	/* try to read in case we're connected */
    clock_delay(x->x_poll, DEFPOLLTIME);	/* see you later */
}

static void *tcpclient_new(t_floatarg udpflag)
{
    int i;

    t_tcpclient *x = (t_tcpclient *)pd_new(tcpclient_class);
    x->x_msgout = outlet_new(&x->x_obj, &s_anything);	/* received data */
    x->x_addrout = outlet_new(&x->x_obj, &s_list);
    x->x_outconnect = outlet_new(&x->x_obj, &s_float);	/* connection state */
    x->x_clock = clock_new(x, (t_method)tcpclient_tick);
    x->x_poll = clock_new(x, (t_method)tcpclient_poll);
    x->x_fd = -1;
    /* convert the bytes in the buffer to floats in a list */
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
    x->x_addr = 0L;
    /* prepare child thread */
    if(pthread_attr_init(&x->x_threadattr) < 0)
        post("%s: warning: could not prepare child thread", objName);
    if(pthread_attr_setdetachstate(&x->x_threadattr, PTHREAD_CREATE_DETACHED) < 0)
        post("%s: warning: could not prepare child thread", objName);
    clock_delay(x->x_poll, 0);	/* start polling the input */
    return (x);
}

static void tcpclient_free(t_tcpclient *x)
{
    tcpclient_disconnect(x);
    clock_free(x->x_poll);
    clock_free(x->x_clock);
}

#ifdef MSW
__declspec(dllexport)
#endif
void tcpclient_setup(void)
{
    tcpclient_class = class_new(gensym(objName), (t_newmethod)tcpclient_new,
        (t_method)tcpclient_free,
        sizeof(t_tcpclient), 0, A_DEFFLOAT, 0);
    class_addmethod(tcpclient_class, (t_method)tcpclient_connect, gensym("connect")
        , A_SYMBOL, A_FLOAT, 0);
    class_addmethod(tcpclient_class, (t_method)tcpclient_disconnect, gensym("disconnect"), 0);
    class_addmethod(tcpclient_class, (t_method)tcpclient_send, gensym("send"), A_GIMME, 0);
    class_addmethod(tcpclient_class, (t_method)tcpclient_rcv, gensym("receive"), 0);
    class_addmethod(tcpclient_class, (t_method)tcpclient_rcv, gensym("rcv"), 0);
    class_addmethod(tcpclient_class, (t_method)tcpclient_dump, gensym("dump"), A_FLOAT, 0);
}

/* end of x_net_tcp.c */
