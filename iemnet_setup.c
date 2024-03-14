/* iemnet
 * this file provides the bootstrap code for building as a multiobject library
 *
 *  copyright © 2010-2024 IOhannes m zmölnig, IEM
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

#include "iemnet.h"

void tcpclient_setup(void);
void tcpreceive_setup(void);
void tcpsend_setup(void);
void tcpserver_setup(void);

void udpclient_setup(void);
void udpreceive_setup(void);
void udpsend_setup(void);
void udpserver_setup(void);


IEMNET_EXTERN void iemnet_setup(void)
{
  tcpclient_setup();
  tcpreceive_setup();
  tcpsend_setup();
  tcpserver_setup();

  udpclient_setup();
  udpreceive_setup();
  udpsend_setup();
  udpserver_setup();
}
