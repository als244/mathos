#include "table.h"
#include "fingerprint.h"

#define SEED 2

typedef struct my_item {
	unsigned char hash_digest[HASH_DIGEST_LEN_BYTES];
	uint64_t db_id;
} My_Item;


/*
The memcmp() function shall return an integer greater than, equal to, or less than 0, 
if the object pointed to by s1 is greater than, equal to, or less than the object pointed to by s2, respectively
*/
int my_item_cmp(void * my_item, void * other_item){
	// printf("In comparing items.\n");
	// printf("\tA. Id: %lu Hash: ", ((My_Item *) my_item) -> db_id);
	// print_sha256(((My_Item *) my_item) -> hash_digest);
	// printf("\tB. Id: %lu Hash: ", ((My_Item *) other_item) -> db_id);
	// print_sha256(((My_Item *) other_item) -> hash_digest);
	int cmp_res = memcmp(my_item, other_item, sizeof(My_Item));
	//printf("Cmp Res: %d\n", cmp_res);
	return cmp_res;
}

// do bitmask of table_size & last 8 bytes of the hash digest
// assuming table size is power of 2
uint64_t my_hash_func(void * my_item, uint64_t table_size){
	My_Item * item_casted = (My_Item *) my_item;
	unsigned char * digest = item_casted -> hash_digest;
	uint64_t least_sig_64bits = sha256_to_least_sig64(digest);
	// compares the least significant log(table_size) bits in the hash
	uint64_t hash_ind = least_sig_64bits & (table_size - 1);
	// printf("In hash function. Working on hash: ");
	// print_sha256(item_casted -> hash_digest);
	// printf("Hash Ind: %lu\n", hash_ind);
	return hash_ind; 
}



// COULD DO ERROR CHECKING HERE...
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

	int ret;
	
	// setting random seed for choosing items to search
	srand(SEED);

	// 1. Fetch Text from Fineweb-Edu Shard to Memory

	printf("Fetching text from fineweb_edu shards...\n");
	
	char * shard_dir = "/mnt/storage/datasets/fineweb_edu/shards/data";
	int shard_id = 0;

	char * buffer;
	uint64_t * record_bytes;
	uint64_t total_records;
	uint64_t total_bytes;

	ret = load_shard(shard_dir, shard_id, &buffer, &record_bytes, &total_records, &total_bytes);
	if (ret != 0){
		fprintf(stderr, "Error: could not load shard with id: %d\n", shard_id);
		exit(1);
	}

	printf("\tRecord Count: %lu\n\tTotal Bytes: %lu\n", total_records, total_bytes);

	// 2. Initialize Table

	uint64_t min_size = 1L << 12;
	uint64_t max_size = 1L << 36;
	float load_factor = .5f;
	float shrink_factor = .1f;
	Hash_Func hash_func = &my_hash_func;
	Item_Cmp item_cmp = &my_item_cmp;
	Table * table = init_table(min_size, max_size, load_factor, shrink_factor, hash_func, item_cmp);

	
	// 3. Start Hashing Records and Inserting Items, Recording Perf

	uint64_t * hash_perf_ns = (uint64_t *) malloc(total_records * sizeof(uint64_t));
	uint64_t * insert_perf_ns = (uint64_t *) malloc(total_records * sizeof(uint64_t));
	uint64_t * search_perf_ns = (uint64_t *) malloc(total_records * sizeof(uint64_t));

	struct timespec start, stop;
	uint64_t timestamp_start, timestamp_stop;
	uint64_t elapsed_ns;


	My_Item ** items = (My_Item **) malloc(total_records * sizeof(My_Item *));

	My_Item * search_ret = NULL;
	uint64_t rand_search_item_id;

	char * cur_record_loc = buffer;

	printf("\nStarting to hash/insert/search...\n");

	My_Item * cur_item;
	for (uint64_t i = 0; i < total_records; i++){
		
		// A.) Set current item
		cur_item = malloc(sizeof(My_Item));
		cur_item -> db_id = i;

		// B.) Do hash of text

		clock_gettime(CLOCK_MONOTONIC, &start);
		
		do_sha256(cur_record_loc, record_bytes[i], cur_item -> hash_digest);

		clock_gettime(CLOCK_MONOTONIC, &stop);

		timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;
		timestamp_stop = stop.tv_sec * 1e9 + stop.tv_nsec;
		elapsed_ns = timestamp_stop - timestamp_start;

		hash_perf_ns[i] = elapsed_ns;

		// setting item in all items array
		items[i] = cur_item;

		// C.) Insert item into hash table

		clock_gettime(CLOCK_MONOTONIC, &start);

		ret = insert_item(table, items[i]);

		clock_gettime(CLOCK_MONOTONIC, &stop);

		timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;
		timestamp_stop = stop.tv_sec * 1e9 + stop.tv_nsec;
		elapsed_ns = timestamp_stop - timestamp_start;

		insert_perf_ns[i] = elapsed_ns;

		// unlikely
		if (ret != 0){
			fprintf(stderr, "Error: could not insert item with id: %lu\n", i);
			exit(1);
		}


		// D.) Do search of random digest previously inserted
		rand_search_item_id = rand() % (i + 1);

		clock_gettime(CLOCK_MONOTONIC, &start);

		search_ret = (My_Item *) find_item(table, items[rand_search_item_id]);

		clock_gettime(CLOCK_MONOTONIC, &stop);

		timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;
		timestamp_stop = stop.tv_sec * 1e9 + stop.tv_nsec;
		elapsed_ns = timestamp_stop - timestamp_start;

		search_perf_ns[i] = elapsed_ns;

		if (search_ret == NULL){
			fprintf(stderr, "Error: could not find item with id (on record insert of %lu): %lu\n", rand_search_item_id, i);
			exit(1);
		}

		// E.) update current record location (include 1 for null terminator)
		cur_record_loc += record_bytes[i] + 1;


		// F.) Print out update of how many records have been inserted
		if ((i + 1) % 1 == 0){
			printf("Hashed/Inserted/Searched: %lu\n", i);
		}
	}


	// 4.) Analyze Performance

	uint64_t num_records_to_avg = total_records;

	uint64_t hash_total_ns = 0;
	uint64_t insert_total_ns = 0;
	uint64_t search_total_ns = 0;
	for (uint64_t i = 0; i < num_records_to_avg; i++){
		hash_total_ns += hash_perf_ns[i];
		insert_total_ns += insert_perf_ns[i];
		search_total_ns += search_perf_ns[i];
	}

	double hash_avg_ns = (double) hash_total_ns / (double) num_records_to_avg;
	double insert_avg_ns = (double) insert_total_ns / (double) num_records_to_avg;
	double search_avg_ns = (double) search_total_ns / (double) num_records_to_avg;


	printf("\n\nAverage ns per operation (over %lu records):\n", num_records_to_avg);
	printf("\tHash: %f\n\tInsert: %f\n\tRandom Search: %f\n\n", hash_avg_ns, insert_avg_ns, search_avg_ns);

	return 0;
}