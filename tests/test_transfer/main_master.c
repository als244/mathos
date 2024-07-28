#include "master.h"


int main(int argc, char * argv[]){

	int ret;

	if (argc != 4){
		fprintf(stderr, "Error: Usage ./testMaster <master_ip_addr> <max_nodes> <min_init_nodes>\n");
		return -1;
	}

	char * ip_addr = argv[1];
	uint32_t max_nodes = (uint32_t) atol(argv[2]);
	uint32_t min_init_nodes = (uint32_t) atol(argv[3]);

	Master * master = init_master(ip_addr, max_nodes, min_init_nodes);
	if (master == NULL){
		fprintf(stderr, "Error: could not initialize master\n");
		return -1;
	}

	printf("Master Configuration:\n");
	printf("\
			IP Address: %s\n \
			Max Workers: %u\n \
			Min Init Nodes: %u\n",
			master -> ip_addr,
			master -> max_nodes,
			master -> min_init_nodes);

	// should ideally never return
	// only shutdown message or error
	ret = run_master(master);
	if (ret != 0){
		fprintf(stderr, "Error: master had an error when exiting: %d\n", ret);
		return ret;
	}

	return 0;

}