#include "exchange.h"

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
		matrix[i] = (float)rand()/(float)(RAND_MAX/a);
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

	// CONFIGURATION
	srand(SEED);
	FingerprintType fingerprint_type = SHA256_HASH;
	uint8_t fingerprint_bytes = get_fingerprint_num_bytes(fingerprint_type);
	

	// 1.) Create Exchange 

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

	int num_args = 2;
	FunctionType function_type = MATMUL;
	DataType data_type = FP32;

	unsigned char ** AB_fingerprints = {A_fingerprint, B_fingerprint};
	unsigned char ** BC_fingerprints = {B_fingerprint, C_fingerprint};
	unsigned char ** CD_fingerprints = {C_fingerprint, D_fingerprint};
	unsigned char ** DA_fingerprints = {D_fingerprint, A_fingerprint};

	Function * f_AB = init_function(function_type, data_type, num_args, AB_fingerprints, fingerprint_bytes);
	Function * f_BC = init_function(function_type, data_type, num_args, BC_fingerprints, fingerprint_bytes);
	Function * f_CD = init_function(function_type, data_type, num_args, BC_fingerprints, fingerprint_bytes);
	Function * f_DA = init_function(function_type, data_type, num_args, BC_fingerprints, fingerprint_bytes);


	// 3.) Obtaining the output fingerprint for each of these functions...

	unsigned char * out_AB = f_AB -> output_fingerprint;
	unsigned char * out_BC = f_BC -> output_fingerprint;
	unsigned char * out_CD = f_CD -> output_fingerprint;
	unsigned char * out_DA = f_DA -> output_fingerprint;


	// 4.) Now create fake memory regions
	
	uint64_t client_0_loc_id = 0;
	uint64_t client_1_loc_id = 1;
	uint64_t client_2_loc_id = 2;


	





}