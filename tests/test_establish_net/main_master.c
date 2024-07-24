#include "master.h"


int main(int argc, char * argv[]){

	int ret;

	if (argc != 3){
		fprintf(stderr, "Error: Usage ./runMaster <master_ip_addr> <max_workers>\n");
		return -1;
	}

	char * ip_addr = argv[1];
	uint32_t max_workers = (uint32_t) atol(argv[2]);

	Master * master = init_master(ip_addr, max_workers);
	if (master == NULL){
		fprintf(stderr, "Error: could not initialize master\n");
		return -1;
	}

	// should ideally never return
	// only shutdown message or error
	ret = run_master(master);
	if (ret != 0){
		fprintf(stderr, "Error: master had an error when exiting: %d\n", ret);
		return ret;
	}

	return 0;

}