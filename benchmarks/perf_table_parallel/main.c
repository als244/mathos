#include "table.h"
#include "fingerprint.h"

#define SEED 1

typedef struct my_item {
	unsigned char * hash_digest;
	uint64_t db_id;
} My_Item;

typedef struct thread_data {
	int thread_num;
	FingerprintType fingerprint_type;
	uint8_t fingerprint_bytes;
	Table * table;
	My_Item ** items;
	uint64_t * record_bytes;
	char * cur_record_loc;
	uint64_t record_start;
	uint64_t thread_num_records;
    uint64_t * hash_perf_ns;
    uint64_t * insert_perf_ns;
    uint64_t * search_perf_ns;
} Thread_Data;


// floor(log2(index) + 1)
// number of low order 1's bits in table_size bitmask
static const char LogTable512[512] = {
	0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9
};




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


// will error if table size is 0 (which should never happen...)
uint64_t my_hash_func(void * my_item, uint64_t table_size){
	My_Item * item_casted = (My_Item *) my_item;
	unsigned char * digest = item_casted -> hash_digest;
	// we were using sha256, so 32 bytes
	uint64_t least_sig_64bits = digest_to_least_sig64(digest, 32);
	// bitmask should be the lower log(2) lower bits of table size.
	// i.e. if table_size = 2^12, we should have a bit mask of 12 1's
	uint64_t bit_mask;
	uint64_t hash_ind;

	// optimization for power's of two table sizes, no need for leading-zero/table lookup or modulus
	if (__builtin_popcountll(table_size) == 1){
		bit_mask = table_size - 1;
		hash_ind = least_sig_64bits & bit_mask;
		return hash_ind;
	}

	int leading_zeros = __builtin_clzll(table_size);
	// 64 bits as table_size type
	// taking ceil of power of 2, then subtracing 1 to get low-order 1's bitmask
	int num_bits = (64 - leading_zeros) + 1;
	bit_mask = (1L << num_bits) - 1;
	hash_ind = (least_sig_64bits & bit_mask) % table_size;
	return hash_ind; 
	
	/* WITHOUT USING BUILT-IN leading zeros...

	// Ref: "https://graphics.stanford.edu/~seander/bithacks.html#ModulusDivisionEasy"
	// here we set the log table to be floor(log(ind) + 1), which represents number of bits we should have in bitmask before modulus
	uint64_t num_bits;
	register uint64_t temp;
	if (temp = table_size >> 56){
		num_bits = 56 + LogTable512[temp];
	}
	else if (temp = table_size >> 48) {
		num_bits = 48 + LogTable512[temp];
	}
	else if (temp = table_size >> 40){
		num_bits = 40 + LogTable512[temp];
	}
	else if (temp = table_size >> 32){
		num_bits = 32 + LogTable512[temp];
	}
	else if (temp = table_size >> 24){
		num_bits = 24 + LogTable512[temp];
	}
	else if (temp = table_size >> 16){
		num_bits = 18 + LogTable512[temp];
	}
	else if (temp = table_size >> 8){
		num_bits = 8 + LogTable512[temp];
	}
	else{
		num_bits = LogTable512[temp];
	}
	bit_mask = (1 << num_bits) - 1;
	// now after computing bit_mask the hash_ind may be greater than table_size
	hash_ind = (least_sig_64bits & bit_mask) % table_size;
	*/
	
	// printf("In hash function. Working on hash: ");
	// print_sha256(item_casted -> hash_digest);
	// printf("Hash Ind: %lu\n", hash_ind);
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

void build_thread_ranges(int num_threads, uint64_t total_records, char * buffer_start, uint64_t * record_bytes, Thread_Data *** ret_all_threads){

	Thread_Data ** all_threads = (Thread_Data **) malloc(num_threads * sizeof(Thread_Data *));

	Thread_Data * cur_thread;

	uint64_t records_per_thread = total_records / (uint64_t) num_threads;
	uint64_t remain_records = total_records % (uint64_t) num_threads;
	
	char * cur_record_loc = buffer_start;
	
	uint64_t cur_record_start = 0;
	uint64_t total_thread_bytes = 0;
	for (int i = 0; i < num_threads; i++){
		cur_thread = malloc(sizeof(Thread_Data));
		cur_thread -> thread_num = i;
		cur_thread -> cur_record_loc = cur_record_loc;
		cur_thread -> record_start = cur_record_start;
		cur_thread -> thread_num_records = records_per_thread;

		if (i < remain_records){
			cur_thread -> thread_num_records += 1;
		}

		total_thread_bytes = 0;
		for (uint64_t j = cur_record_start; j < cur_record_start + cur_thread -> thread_num_records; j++){
			// need to add 1 for null terminator
			total_thread_bytes += record_bytes[j] + 1;
		}


		cur_record_start += cur_thread -> thread_num_records;
		cur_record_loc += total_thread_bytes;

		all_threads[i] = cur_thread;
	}

	*ret_all_threads = all_threads;

	return;
}


void * do_work(void * _thread_data){


	int ret;

	Thread_Data * thread_data = (Thread_Data *) _thread_data;

	FingerprintType fingerprint_type = thread_data -> fingerprint_type;
	uint8_t fingerprint_bytes = thread_data -> fingerprint_bytes;
	Table * table = thread_data -> table;
	My_Item ** items = thread_data -> items;
	uint64_t * record_bytes = thread_data -> record_bytes;
	char * cur_record_loc = thread_data -> cur_record_loc;
	uint64_t record_start = thread_data -> record_start;
	uint64_t num_records = thread_data -> thread_num_records;
	uint64_t * hash_perf_ns = thread_data -> hash_perf_ns;
	uint64_t * insert_perf_ns = thread_data -> insert_perf_ns;
	uint64_t * search_perf_ns = thread_data -> search_perf_ns;
	int thread_num = thread_data -> thread_num;

	

	struct timespec start, stop;
	uint64_t timestamp_start, timestamp_stop;
	uint64_t elapsed_ns;

	My_Item * cur_item;
	My_Item * search_ret = NULL;
	uint64_t rand_search_item_id;

	for (uint64_t i = record_start; i < record_start + num_records; i++){
		
		// A.) Set current item
		cur_item = malloc(sizeof(My_Item));
		cur_item -> hash_digest = malloc(fingerprint_bytes);
		cur_item -> db_id = i;

		// B.) Do hash of text

		clock_gettime(CLOCK_MONOTONIC, &start);
		
		// function call overhead may add a few 10s of nanos
		do_fingerprinting(cur_record_loc, record_bytes[i], cur_item -> hash_digest, fingerprint_type);

		clock_gettime(CLOCK_MONOTONIC, &stop);

		timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;
		timestamp_stop = stop.tv_sec * 1e9 + stop.tv_nsec;
		elapsed_ns = timestamp_stop - timestamp_start;

		hash_perf_ns[i] = elapsed_ns;

		// setting item in all items array
		items[i] = cur_item;

		// C.) Insert item into hash table

		clock_gettime(CLOCK_MONOTONIC, &start);

		ret = insert_item_table(table, items[i], thread_num);

		clock_gettime(CLOCK_MONOTONIC, &stop);

		timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;
		timestamp_stop = stop.tv_sec * 1e9 + stop.tv_nsec;
		elapsed_ns = timestamp_stop - timestamp_start;

		insert_perf_ns[i] = elapsed_ns;

		// unlikely
		if (ret != 0){
			fprintf(stderr, "Error: could not insert item with id: %lu\n", i);
			return NULL;
		}


		// D.) Do search of random digest previously inserted
		// because we are sharding across threads we should choose one within this thread's range
		rand_search_item_id = (rand() % (i + 1 - record_start)) + record_start;

		clock_gettime(CLOCK_MONOTONIC, &start);

		search_ret = (My_Item *) find_item_table(table, items[rand_search_item_id]);

		clock_gettime(CLOCK_MONOTONIC, &stop);

		timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;
		timestamp_stop = stop.tv_sec * 1e9 + stop.tv_nsec;
		elapsed_ns = timestamp_stop - timestamp_start;

		search_perf_ns[i] = elapsed_ns;

		if (search_ret == NULL){
			fprintf(stderr, "Error: could not find item with id (on record insert of %lu): %lu\n", i, rand_search_item_id);
			return NULL;
		}

		// E.) update current record location (include 1 for null terminator)
		cur_record_loc += record_bytes[i] + 1;


		// F.) Print out update of how many records have been inserted
		// if ((i + 1) % 1 == 0){
		// 	printf("Hashed/Inserted/Searched: %lu\n", i);
		// }
	}
}



int main(int argc, char * argv[]){

	int ret;


	// setting random seed for choosing items to search
	srand(SEED);
	FingerprintType fingerprint_type = SHA256_HASH;
	uint8_t fingerprint_bytes = get_fingerprint_num_bytes(fingerprint_type);
	
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

	uint64_t min_size = 1UL << 12;
	uint64_t max_size = 1UL << 36;
	float load_factor = .5f;
	float shrink_factor = .1f;
	Hash_Func hash_func = &my_hash_func;
	Item_Cmp item_cmp = &my_item_cmp;
	Table * table = init_table(min_size, max_size, load_factor, shrink_factor, hash_func, item_cmp);
	if (table == NULL){
		fprintf(stderr, "Erro: could not init table\n");
		exit(1);
	}

	
	// 3. Start Hashing Records and Inserting Items, Recording Perf

	uint64_t * hash_perf_ns = (uint64_t *) malloc(total_records * sizeof(uint64_t));
	uint64_t * insert_perf_ns = (uint64_t *) malloc(total_records * sizeof(uint64_t));
	uint64_t * search_perf_ns = (uint64_t *) malloc(total_records * sizeof(uint64_t));
;

	struct timespec total_start, total_stop;
	uint64_t timestamp_total_start, timestamp_total_stop;
	uint64_t total_elapsed_ns;

	My_Item ** items = (My_Item **) calloc(total_records, sizeof(My_Item *));

	// 4.) Build thread meta data
	
	int num_threads = 16;

	pthread_t * threads = (pthread_t *) malloc(num_threads * sizeof(pthread_t));

	Thread_Data ** all_threads;
	build_thread_ranges(num_threads, total_records, buffer, record_bytes, &all_threads);

	for (int i = 0; i < num_threads; i++){
		all_threads[i] -> fingerprint_type = fingerprint_type;
		all_threads[i] -> fingerprint_bytes = fingerprint_bytes;
		all_threads[i] -> table = table;
		all_threads[i] -> items = items;
		all_threads[i] -> record_bytes = record_bytes;
		all_threads[i] -> hash_perf_ns = hash_perf_ns;
		all_threads[i] -> insert_perf_ns = insert_perf_ns;
		all_threads[i] -> search_perf_ns = search_perf_ns;
		pthread_create(&threads[i], NULL, do_work, (void *) all_threads[i]);
	}

	// 5.) Start the threads

	clock_gettime(CLOCK_MONOTONIC, &total_start);

	for (int i = 0; i < num_threads; i++){
		pthread_join(threads[i], NULL);
	}

	clock_gettime(CLOCK_MONOTONIC, &total_stop);
	timestamp_total_start = total_start.tv_sec * 1e9 + total_start.tv_nsec;
	timestamp_total_stop = total_stop.tv_sec * 1e9 + total_stop.tv_nsec;
	total_elapsed_ns = timestamp_total_stop - timestamp_total_start;
	uint64_t total_elapsed_ms = total_elapsed_ns / 1e6;


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


	printf("\n\nAverage ns per operation. %lu records, hashing with type %s:\n", num_records_to_avg, get_fingerprint_type_name(fingerprint_type));
	printf("\tHash: %f\n\tInsert: %f\n\tRandom Search: %f\n\n", hash_avg_ns, insert_avg_ns, search_avg_ns);
	printf("Total Elapsed Millis: %lu\n\n", total_elapsed_ms);

	return 0;
}