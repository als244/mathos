#include "init_net.h"

int main(int argc, char * argv[]){

	if ((argc != 2) && (argc != 3)){
		fprintf(stderr, "Error: Usage ./testWorker <master_ip_addr> <self_ip_addr>\n");
		return -1;
	}
	
	char * master_ip_addr = argv[1];
	
	char * self_ip_addr = NULL;
	if (argc == 3){
		self_ip_addr = argv[2];
	}	

	Net_World * net_world = init_net(master_ip_addr, self_ip_addr);

	if (net_world == NULL){
		fprintf(stderr, "Error: failed to initialize net\n");
		return -1;
	}

	return 0;
}