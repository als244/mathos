#include "exchange.h"


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


int main(int argc, char * argv[]){

	// CONFIGURATION
	srand(SEED);
	FingerprintType fingerprint_type = SHA256_HASH;
	uint8_t fingerprint_bytes = get_fingerprint_num_bytes(fingerprint_type);
	
	// Only 1 exchange so doing full range	
	uint64_t start_val = 0;
	// wrap around, max value
	uint64_t end_val = -1;

	uint64_t max_bids = 1 << 36;
	uint64_t max_offers = 1 << 36;


	// 1.) Getting some axiomatic data to work with 




	// 2.) Calling functions on these data to generate fingerprints for output data that can be used as inputs for future functions

	Function * matmulinit_function(FunctionType function_type, DataType data_type, int num_args, unsigned char ** argument_fingerprints, uint8_t fingerprint_bytes);


}