#include "skiplist.h"


Skiplist * init_skiplist(Item_Cmp key_cmp, uint8_t max_levels, float level_factor, uint64_t gc_cap) {

	Skiplist * skiplist = (Skiplist *) malloc(sizeof(Skiplist));
	if (skiplist == NULL){
		fprintf(stderr, "Error: malloc failed to allocate skiplist\n");
		return NULL;
	}

	skiplist -> max_levels = level_factor;
	skiplist -> level_factor = level_factor;
	skiplist -> key_cmp = key_cmp;


	Skiplist_Item ** level_lists = (Skiplist_Item **) malloc(max_levels * sizeof(Skiplist_Item *));
	if (level_lists == NULL){
		fprintf(stderr, "Error: malloc failed to allocate level_lists container\n");
		return NULL;
	}
	for (uint8_t i = 0; i < max_levels; i++){
		level_lists[i] = NULL;
	}

	skiplist -> level_lists = level_lists;

	pthread_mutex_t * level_locks = (pthread_mutex_t *) malloc(max_levels * sizeof(pthread_mutex_t));
	if (level_locks == NULL){
		fprintf(stderr, "Error: malloc failed to allocate level_locks container\n");
		return NULL;
	}

	int ret;

	
	for (uint8_t i = 0; i < max_levels; i++){
		ret = pthread_mutex_init(&(level_locks[i]), NULL);
		if (ret != 0){
			fprintf(stderr, "Error: could not init level lock\n");
			return NULL;
		}

	}

	skiplist -> level_locks = level_locks;

	skiplist -> cur_max_level_hint = 0;


	Level_Range * level_ranges = (Level_Range *) malloc(max_levels * sizeof(Level_Range));
	if (level_ranges == NULL){
		fprintf(stderr, "Error: malloc failed to allocate level ranges\n");
		return NULL;
	}

	float cur_start = 0;
	float prev_prob = 1;
	float level_prob;
	for (uint8_t i = 0; i < max_levels; i++){
		level_ranges[i].level = i;
		level_ranges[i].start = cur_start;
		level_prob = prev_prob * skiplist -> level_factor;
		level_ranges[i].stop = cur_start + level_prob;
		// update for next level
		cur_start = level_ranges[i].stop;
		prev_prob = level_prob;
	}

	skiplist -> rand_level_upper_bound = cur_start;





	skiplist -> num_active_ops = 0;

	skiplist -> gc_cap = gc_cap;
	skiplist -> cur_gc_cnt = 0;

	ret = pthread_mutex_init(&(skiplist -> gc_cnt_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init gc cnt lock\n");
		return NULL;
	}


	ret = pthread_mutex_init(&(skiplist ->  num_active_ops_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init num_active_ops lock\n");
		return NULL;
	}


	ret = pthread_cond_init(&(skiplist -> gc_cv), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init gc_cv\n");
	}

	void ** delete_bin = (void **) malloc(gc_cap * sizeof(void *));
	if (delete_bin == NULL){
		fprintf(stderr, "Error: malloc failed to allocate delete_bin\n");
		return NULL;
	}

	skiplist -> delete_bin = delete_bin;

	return skiplist;
}


// For a given starting item, key, and level
// Obtain the lock for the predecessor of where key would be inserted
Skiplist_Item * get_forward_lock(Skiplist * skiplist, Skiplist_Item * skiplist_item, void * key, uint8_t level){


	Skiplist_Item * cur_skiplist_item = skiplist_item;

	// Search without locking to find next predecessor
	while (cur_skiplist_item != NULL && 
				((skiplist -> key_cmp)(key, (void *) ((cur_skiplist_item -> forward)[level])) < 0)){
		cur_skiplist_item = (cur_skiplist_item -> forward)[level];
	}

	// if there is no predecessor that is smaller than key
	//	- either empty list or smallest element in list
	if (cur_skiplist_item == NULL){
		return NULL;
	}	

	// Now do the search while acquiring locks in case there were
	// inserts between loop advancing (94-96) and now
	// The loop got us as far as possible without requiring locking
	cur_skiplist_item = (cur_skiplist_item -> forward)[level];
	pthread_mutex_lock(&((cur_skiplist_item -> forward_locks)[level]));

	// We know there is at least 1 element in the list that is smaller than key now (due to check on line 101)
	// Also comparing key, NULL returns a positive value (null is greater than key), so we will not
	// advance the current skip list item to be null
	while ((skiplist -> key_cmp)(key, (void *) ((cur_skiplist_item -> forward)[level])) < 0){
		pthread_mutex_unlock(&((cur_skiplist_item -> forward_locks)[level]));
		cur_skiplist_item = (cur_skiplist_item -> forward)[level];
		pthread_mutex_lock(&((cur_skiplist_item -> forward_locks)[level]));
	}

	// Now the rightmost element that is smaller than key is locked
	return cur_skiplist_item;
}

int level_range_cmp(const void * range_a, const void * range_b){

	float val = ((Level_Range *) range_a) -> start;
	float range_val_start = ((Level_Range *) range_b) -> start;
	float range_val_stop = ((Level_Range *) range_b) -> stop;

	if (val >= range_val_start && val <= range_val_stop){
		return 0;
	}
	else if (val < range_val_start){
		return -1;
	}
	else{
		return 1;
	}
}

Skiplist_Item * init_skiplist_item(Skiplist * skiplist, void * key, void * value){

	Skiplist_Item * skiplist_item = (Skiplist_Item *) malloc(sizeof(Skiplist_Item));
	if (skiplist_item == NULL){
		fprintf(stderr, "Error: malloc failed to allocate skiplist item\n");
		return NULL;
	}

	skiplist_item -> key = key;

	// no need for comparing items in this deque
	Deque * value_list = init_deque(NULL);

	int ret = insert_deque(value_list, BACK_DEQUE, value);
	if (ret != 0){
		fprintf(stderr, "Error: could not insert item into newly intialized deque\n");
		return NULL;
	}

	skiplist_item -> value_list = value_list;

	// Determine level for item

	// between 0 and 1
	float rand_val = (float)rand() / (float)RAND_MAX;
	// now force between 0 and skiplist -> rand_level_upper_bound
	float rand_level_val = rand_val * skiplist -> rand_level_upper_bound;

	Level_Range dummy_range;
	dummy_range.start = rand_level_val;

	Level_Range * chosen_level = bsearch(&dummy_range, skiplist -> level_ranges, skiplist -> max_levels, sizeof(Level_Range), level_range_cmp);
	if (chosen_level == NULL){
		fprintf(stderr, "Error: could not find chosen level\n");
		return NULL;
	}

	uint8_t level = chosen_level -> level;

	skiplist_item -> level = level;

	Skiplist_Item ** forward = (Skiplist_Item **) calloc((level + 1), sizeof(Skiplist_Item *));
	if (forward == NULL){
		fprintf(stderr, "Error: malloc failed to allocate forward array for skiplist_item\n");
		return NULL;
	}

	skiplist_item -> forward = forward;

	pthread_mutex_t * forward_locks = (pthread_mutex_t *) malloc((level + 1) * sizeof(pthread_mutex_t));
	if (forward_locks == NULL){
		fprintf(stderr, "Error: failure to allocate forward_locks\n");
		return NULL;
	}

	for (uint8_t i = 0; i < level + 1; i++){
		ret = pthread_mutex_init(&(forward_locks[i]), NULL);
		if (ret != 0){
			fprintf(stderr, "Error: could not init forward lock\n");
			return NULL;
		}
	}

	skiplist_item -> forward_locks = forward_locks;

	return skiplist_item;
}


// Appends value to the skiplist_item -> value_list that matches key. If no key exists, creates a skiplist_item
// and intializes a deque with value
int insert_item_skiplist(Skiplist * skiplist, void * key, void * value) {

	
	// indicate that we are doing an operation
	pthread_mutex_lock(&skiplist -> num_active_ops_lock);
	skiplist -> num_active_ops += 1;
	pthread_mutex_unlock(&skiplist -> num_active_ops_lock);


	uint8_t cur_max_level = skiplist -> cur_max_level_hint;

	Skiplist_Item * cur_skiplist_item;

	uint8_t max_levels = skiplist -> max_levels;
	void * prev_items_per_level[max_levels];
	for (uint8_t i = 0; i < max_levels; i++){
		prev_items_per_level[i] = NULL;
	}

	// find the rightmost elements at each level that are less than key
	for (uint8_t cur_level = cur_max_level; cur_level >= 0; cur_level--){

		cur_skiplist_item = (skiplist -> level_lists)[cur_level];
		// Advancing to the maximum element less than key
		while (cur_skiplist_item != NULL && 
				((skiplist -> key_cmp)(key, (void *) ((cur_skiplist_item -> forward)[cur_level])) < 0)) {
			cur_skiplist_item = (cur_skiplist_item -> forward)[cur_level];
		}
		prev_items_per_level[cur_level] = cur_skiplist_item;
	}

	// Determine if this element is already in the skiplist
	// If it is in the skiplist then 
	
	// Use the unlocked search as guide for rightmost
	Skiplist_Item * rightmost_base_level_hint = prev_items_per_level[0];

	// Actually determine the current rightmost and acquire lock
	Skiplist_Item * rightmost_base_level = get_forward_lock(skiplist, rightmost_base_level_hint, key, 0);

	int ret;

	// if this item already exists, just add value to the deque 
	//	(which is thread safe)
	if (rightmost_base_level != NULL &&
			((skiplist -> key_cmp)(key, ((rightmost_base_level -> forward)[0])) == 0)){

		// now add the value to the value_list deque
		// can only fail on OOM 
		ret = insert_deque(rightmost_base_level -> value_list, BACK_DEQUE, value);
		pthread_mutex_unlock(&((rightmost_base_level -> forward_locks)[0]));
		return ret;
	}


	// Otherwise we need to create a skiplist_item
	Skiplist_Item * new_item = init_skiplist_item(skiplist, key, value);
	if (new_item == NULL){
		fprintf(stderr, "Error: failure to init new skiplist_item\n");
		return -1;
	}

	// now need to add this item!





}


// Removes element from skiplist_item -> value_list where the item is the minimum key >= key
// if no skiplist item (== value_list is NULL) then return NULL
void * take_closest_item_skiplist(Skiplist * skiplist, void * key) {





}