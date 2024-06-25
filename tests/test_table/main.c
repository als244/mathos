#include "table.h"
#include "fingerprint.h"

typedef struct my_item {
	unsigned char hash_digest[HASH_DIGEST_LEN_BYTES];
	void * phys_ref;
} My_Item;


/*
The memcmp() function shall return an integer greater than, equal to, or less than 0, 
if the object pointed to by s1 is greater than, equal to, or less than the object pointed to by s2, respectively
*/
int my_item_cmp(void * my_item, void * other_item){
	return memcmp(my_item, other_item, sizeof(My_Item));
}

// do bitmask of table_size & last 8 bytes of the hash digest
// assuming table size is power of 2
uint64_t my_hash_func(void * my_item, uint64_t table_size){
	My_Item * item_casted = (My_Item *) my_item;
	unsigned char * digest = item_casted -> hash_digest;
	uint64_t least_sig_64bits = sha256_to_least_sig64(digest);
	// compares the least significant log(table_size) bits in the hash
	return least_sig_64bits & (table_size - 1); 
}


int main(int argc, char * argv[]){

	uint64_t table_size = 1 << 12;

	printf("Ensuring SHA256 and bitmasking with table size is working...\n\n");
	char * example_data = "abc";
	uint64_t num_bytes = 3;
	My_Item my_item;
	do_sha256((void *) example_data, num_bytes, my_item.hash_digest);
	printf("\tHash of %s: ", example_data);
	print_sha256(my_item.hash_digest);
	uint64_t hash_ind = my_hash_func((void *)&my_item, table_size);
	printf("\tHash Ind of %s (with table size of %lu): %lu\n\n", example_data, table_size, hash_ind);
	

	char * example_other_data = "abcd";
	uint64_t other_num_bytes = 4;
	My_Item other_my_item;
	do_sha256((void *) example_other_data, other_num_bytes, other_my_item.hash_digest);
	printf("\tHash of %s: ", example_other_data);
	print_sha256(other_my_item.hash_digest);
	uint64_t other_hash_ind = my_hash_func((void *)&other_my_item, table_size);
	printf("\tHash Ind of %s (with table size of %lu): %lu\n\n", example_other_data, table_size, other_hash_ind);
	
	int cmp_ret = my_item_cmp((void *)&my_item, (void *)&other_my_item);
	printf("\tItem compare returns: %d\n\n\n", cmp_ret);

	uint64_t min_size = 1L << 12;
	uint64_t max_size = 1L << 36;
	float load_factor = .5f;
	float shrink_factor = .1f;
	Hash_Func hash_func = &my_hash_func;
	Item_Cmp item_cmp = &my_item_cmp;
	Table * table = init_table(min_size, max_size, load_factor, shrink_factor, hash_func, item_cmp);

	printf("Ensuring Hash Table Inserting/Searching/Removing is working...\n\n");

	int ret;
	printf("\tInserting hash of %s\n", example_data);
	ret = insert_item(table, (void *)&my_item);
	if (ret != 0){
		fprintf(stderr, "\t\tError: could not insert item\n");
		exit(1);
	}

	printf("\n\n");

	printf("\tSearching for hash of %s\n", example_data);
	void * ret_item = find_item(table, &my_item);
	if (ret_item){
		printf("\t\tFound item. Hash: ");
		print_sha256(((My_Item *) ret_item) -> hash_digest);
	}
	else {
		printf("\t\tCould not find item\n");
	}

	printf("\n\n");


	printf("\tSearching for hash of %s (expecting not found)\n", example_other_data);
	ret_item = find_item(table, &other_my_item);
	if (ret_item){
		printf("\t\tFound item. Hash: ");
		print_sha256(((My_Item *) ret_item) -> hash_digest);
	}
	else {
		printf("\t\tCould not find item\n");
	}

	printf("\n\n");


	printf("\tInserting hash of %s\n", example_other_data);
	ret = insert_item(table, (void *)&other_my_item);
	if (ret != 0){
		fprintf(stderr, "\t\tError: could not insert item\n");
		exit(1);
	}

	printf("\n\n");
	
	printf("\tSearching for hash of %s\n", example_other_data);
	ret_item = find_item(table, &other_my_item);
	if (ret_item){
		printf("\t\tFound item. Hash: ");
		print_sha256(((My_Item *) ret_item) -> hash_digest);
	}
	else {
		printf("\t\tCould not find item\n");
	}

	printf("\n\n");

	printf("\tRemoving hash of %s\n", example_data);
	ret_item = remove_item(table, &my_item);
	if (ret_item){
		printf("\t\tRemoved item. Hash: ");
		print_sha256(((My_Item *) ret_item) -> hash_digest);
	}
	else{
		printf("\t\tCould not find item to remove\n");
	}

	printf("\n\n");

	printf("\tSearching for hash of %s (expecting not found)\n", example_data);
	ret_item = find_item(table, &my_item);
	if (ret_item){
		printf("\t\tFound item. Hash: ");
		print_sha256(((My_Item *) ret_item) -> hash_digest);
	}
	else {
		printf("\t\tCould not find item\n");
	}





}