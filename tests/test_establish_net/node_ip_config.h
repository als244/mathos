#ifndef NODE_IP_CONFIG_H
#define NODE_IP_CONFIG_H

#include "common.h"
#include "table.h"

typedef struct node_ip_config {
	// equivalent to inet_addr(char *)
	// ip_addr is in network-byte order (not host order)
	uint32_t ip_addr;
	uint32_t node_id;
} Node_Ip_Config;

int populate_node_ip_tables(Table * ip_to_node, Table * node_to_ip, char * node_ip_config_filename);

#endif