#include "init_sys.h"


System * init_system(char * master_ip_addr, char * self_ip_addr){

	System * system = (System *) malloc(sizeof(System));
	if (system == NULL){
		fprintf(stderr, "Error: malloc failed to allocate system struct container\n");
		return NULL;
	}

	Exchange * exchange = init_exchange();
	if (exchange == NULL){
		fprintf(stderr, "Error: failed to initialize exchange\n");
		return NULL;
	}


	Net_World * net_world = init_net(master_ip_addr, self_ip_addr);
	if (net_world == NULL){
		fprintf(stderr, "Error: failed to initialize network\n");
		return NULL;
	}

	system -> exchange = exchange;
	system -> net_world = net_world;

	return system;
}