#include "iemnet.h"

void tcpclient_setup(void);
void tcpserver_setup(void);


IEMNET_EXTERN void iemnet_setup(void) {
tcpserver_setup();
tcpclient_setup();


}