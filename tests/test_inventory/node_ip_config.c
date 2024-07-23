#include "node_ip_config.h"

int populate_node_ip_tables(Table * ip_to_node, Table * node_to_ip, char * node_ip_config_filename) {

	FILE * fp = fopen(node_ips_config_filename, "r");
	if (fp == NULL){
		fprintf(stderr, "Error: could not open node_ips config file\n");
		return -1;
	}

	char * line = NULL;
    size_t len = 0;

    uint32_t node_id;
    uint32_t ip_addr;

    int n_items_read;

    int insert_ret;

    int line_cnt = 0;

    Node_Ip_Config * node_ip_config;
    while (getline(&line, &len, fp) != -1) {

    	// 1.) Parse config file line. Get node and ip address from line. Format is: node_id ip_addr
    	//		- where ip_addr is the network-orderd uint32 version of ip address
    	
    	n_items_read = sscanf(line, "%u %u", &node_id, &ip_addr);
    	if (n_items_read != 2){
    		fprintf(stderr, "Error: bad node_ips config file. Only read %d items on line %d. Was expecting to read 2 items <node_id ip_addr>\n", n_items_read, line_cnt);
    		return -1;
    	}

    	// 2.) Create and set data structure to be inserted into table
    	node_ip_config = (Node_Ip_Config *) malloc(sizeof(Node_Ip_Config));
    	if (node_ip_config == NULL){
    		fprintf(stderr, "Error: malloc failed to allocate node_ip_config before adding to table\n");
    		return -1;
    	}

    	node_ip_config -> node_id = node_id;
    	node_ip_config -> ip_addr = ip_addr;

    	// 3.) Insert item into both tables 
    	// (upon removal, need to ensure removing from both, but only freeing once)

    	insert_ret = insert_item_table(ip_to_node, node_ip_config);
    	if (insert_ret != 0){
    		fprintf(stderr, "Error: could not insert into ip_to_node table. (node_id: %u, ip_addr: %u, line #: %d)\n", node_id, ip_addr, line_cnt);
    		return -1;
    	}


    	insert_ret = insert_item_table(node_to_ip, node_ip_config);
    	if (insert_ret != 0){
    		fprintf(stderr, "Error: could not insert into node_to_ip table. (node_id: %u, ip_addr: %u, line #: %d)\n", node_id, ip_addr, line_cnt);
    		return -1;
    	}

    	// 4.) update line count in case of error (want it to be printed)
    	line_cnt++;
    }

    fclose(fp);

    return 0;
}