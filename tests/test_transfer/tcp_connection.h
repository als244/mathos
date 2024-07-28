#ifndef TCP_CONNECTION_H
#define TCP_CONNECTION_H

#include "common.h"

int connect_to_server(char * server_ip_addr, char * self_ip_addr, unsigned short server_port);

#endif