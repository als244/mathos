#include "exchange.h"
#include "function.h"

#define SEED 1

int * generate_rand_int_matrix(uint64_t m, uint64_t n){

	int * matrix = malloc(m * n * sizeof(int));

	uint64_t num_entries = m * n;
	for (uint64_t i = 0; i < num_entries; i++){
		matrix[i] = rand();
	}

	return matrix;
}


float * generate_rand_float_matrix(uint64_t m, uint64_t n){

	float * matrix = malloc(m * n * sizeof(float));

	uint64_t num_entries = m * n;
	for (uint64_t i = 0; i < num_entries; i++){
		matrix[i] = (float)rand()/(float)(RAND_MAX);
	}

	return matrix;
}

int load_shard(char * shard_dir, int shard_id, char ** ret_buffer, uint64_t ** ret_record_bytes, uint64_t * ret_total_records, uint64_t * ret_total_bytes){

	// Dumping Record Cnt

	uint64_t record_cnt;
	uint64_t shard_bytes;

    char * shard_record_cnt_path;
    asprintf(&shard_record_cnt_path, "%s/%04d.counts", shard_dir, shard_id);
    FILE *shard_record_cnt_file = fopen(shard_record_cnt_path, "r");
    free(shard_record_cnt_path);
    fscanf(shard_record_cnt_file, "%lu %lu", &record_cnt, &shard_bytes);
    fclose(shard_record_cnt_file);

    uint64_t * record_bytes = (uint64_t *) malloc(record_cnt * sizeof(uint64_t));

    // Dumping Record Bytes
    char * shard_record_bytes_path;
    asprintf(&shard_record_bytes_path, "%s/%04d.lengths", shard_dir, shard_id);
    FILE *shard_record_bytes_file = fopen(shard_record_bytes_path, "rb");
    free(shard_record_bytes_path);
    fread(record_bytes, sizeof(uint64_t), record_cnt, shard_record_bytes_file);
    fclose(shard_record_bytes_file);

    char * buffer = malloc(shard_bytes);

	// Dumping Buffer
    char * shard_text_path;
    asprintf(&shard_text_path, "%s/%04d.txt", shard_dir, shard_id);
	FILE *shard_text_file = fopen(shard_text_path, "rb");
	free(shard_text_path);
	fread(buffer, sizeof(char), shard_bytes, shard_text_file);
	fclose(shard_text_file);

	*ret_buffer = buffer;
	*ret_record_bytes = record_bytes;
	*ret_total_records = record_cnt;
	// here total bytes includes null-terminators...
	*ret_total_bytes = shard_bytes;

    return 0;   

}

// NOT DOING MUCH ERROR CHECKING HERE...

int main(int argc, char * argv[]){

	int ret;

	// CONFIGURATION
	srand(SEED);
	FingerprintType fingerprint_type = SHA256_HASH;
	uint8_t fingerprint_bytes = get_fingerprint_num_bytes(fingerprint_type);
	

	// 1.) Create Exchange 
	//printf("Initializing Exchange...\n");

	uint64_t exchange_id = 0;

	// Only 1 exchange so doing full range	
	uint64_t start_val = 0;
	// wrap around, max value
	uint64_t end_val = -1;

	uint64_t max_bids = 1 << 36;
	uint64_t max_offers = 1 << 36;

	Exchange * exchange = init_exchange(exchange_id, start_val, end_val, max_bids, max_offers);
	if (exchange == NULL){
		fprintf(stderr, "Error: could not initialize exchange\n");
		exit(1);
	}


	// 2.) Getting some axiomatic data to work with (ignoring chunks and obj id mappings for now...)
	//printf("Generating Fake Axiomatic Data...\n");

	float * A = generate_rand_float_matrix(4096, 8192);
	uint64_t A_size = 4096 * 8192 * sizeof(float);
	float * B = generate_rand_float_matrix(8192, 1024);
	uint64_t B_size = 8192 * 1024 * sizeof(float);
	float * C = generate_rand_float_matrix(1024, 2048);
	uint64_t C_size = 1024 * 2048 * sizeof(float);
	float * D = generate_rand_float_matrix(2048, 4096);
	uint64_t D_size = 2048 * 4096 * sizeof(float);

	unsigned char * A_fingerprint = malloc(fingerprint_bytes);
	unsigned char * B_fingerprint = malloc(fingerprint_bytes);
	unsigned char * C_fingerprint = malloc(fingerprint_bytes);
	unsigned char * D_fingerprint = malloc(fingerprint_bytes);

	do_fingerprinting(A, A_size, A_fingerprint, fingerprint_type);
	do_fingerprinting(B, B_size, B_fingerprint, fingerprint_type);
	do_fingerprinting(C, C_size, C_fingerprint, fingerprint_type);
	do_fingerprinting(D, D_size, D_fingerprint, fingerprint_type);


	// 3.) Calling functions on these data to generate fingerprints for output data that can be used as inputs for future functions
	//printf("Generating Fake Axiomatic Functions...\n");
	int num_args = 2;
	FunctionType function_type = MATMUL;
	DataType data_type = FP32;

	unsigned char ** AB_fingerprints = malloc(2 * sizeof(unsigned char *));
	AB_fingerprints[0] = A_fingerprint;
	AB_fingerprints[1] = B_fingerprint;
	unsigned char ** BC_fingerprints = malloc(2 * sizeof(unsigned char *));
	BC_fingerprints[0] = B_fingerprint;
	BC_fingerprints[1] = C_fingerprint;
	unsigned char ** CD_fingerprints = malloc(2 * sizeof(unsigned char *));
	CD_fingerprints[0] = C_fingerprint;
	CD_fingerprints[1] = D_fingerprint;
	unsigned char ** DA_fingerprints = malloc(2 * sizeof(unsigned char *));
	DA_fingerprints[0] = D_fingerprint;
	DA_fingerprints[1] = A_fingerprint;

	Function * f_AB = init_function(function_type, data_type, num_args, AB_fingerprints, fingerprint_bytes);
	Function * f_BC = init_function(function_type, data_type, num_args, BC_fingerprints, fingerprint_bytes);
	Function * f_CD = init_function(function_type, data_type, num_args, BC_fingerprints, fingerprint_bytes);
	Function * f_DA = init_function(function_type, data_type, num_args, BC_fingerprints, fingerprint_bytes);


	// 3.) Obtaining the output fingerprint for each of these functions...
	//printf("Retrieving encoded axiomatic functions => derived objects...\n");
	unsigned char * out_AB = f_AB -> output_fingerprint;
	unsigned char * out_BC = f_BC -> output_fingerprint;
	unsigned char * out_CD = f_CD -> output_fingerprint;
	unsigned char * out_DA = f_DA -> output_fingerprint;

	uint64_t AB_size = 4096 * 1024 * sizeof(float);
	uint64_t BC_size = 8192 * 2048 * sizeof(float);
	uint64_t CD_size = 1024 * 4096 * sizeof(float);
	uint64_t DA_size = 2048 * 8192 * sizeof(float);


	// 4.) Now create fake memory regions
	//printf("Posting fake bids and offers to exchange...\n");

	printf("Posting initial bid...\n\n");
	// mimic posting bid's and offer's for out_*
	ret = post_bid(exchange, out_BC, fingerprint_bytes, BC_size, 0, 42, 12);    	
	if (ret != 0){
		fprintf(stderr, "Error: could not post bid\n");
		exit(1);
	}

	printf("Posting matching offer against bid...\n\n");
	ret = post_offer(exchange, out_BC, fingerprint_bytes, BC_size, 1, 35, 16);    	
	if (ret != 0){
		fprintf(stderr, "Error: could not post bid\n");
		exit(1);
	}

	printf("Now posting matching bid against offer...\n\n");
	ret = post_bid(exchange, out_BC, fingerprint_bytes, BC_size, 2, 7, 8);    	
	if (ret != 0){
		fprintf(stderr, "Error: could not post bid\n");
		exit(1);
	}



	return 0;



}