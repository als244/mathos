#include "skiplist.h"


Skiplist * init_skiplist(Item_Cmp key_cmp, uint8_t max_levels, float level_factor, uint64_t gc_cap) {

	Skiplist * skiplist = (Skiplist *) malloc(sizeof(Skiplist));
	if (skiplist == NULL){
		fprintf(stderr, "Error: malloc failed to allocate skiplist\n");
		return NULL;
	}

	skiplist -> max_levels = max_levels;
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

	skiplist -> cur_max_level_hint = 0;

	int ret;

	ret = pthread_mutex_init(&(skiplist -> level_hint_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init level hint lock\n");
		return NULL;
	}

	pthread_mutex_t * list_head_locks = (pthread_mutex_t *) malloc(max_levels * sizeof(pthread_mutex_t));
	if (list_head_locks == NULL){
		fprintf(stderr, "Error: malloc failed to allocate list list_head_locks container\n");
		return NULL;
	}

	for (uint8_t i = 0; i < max_levels; i++){
		ret = pthread_mutex_init(&(list_head_locks[i]), NULL);
		if (ret != 0){
			fprintf(stderr, "Error: could not init level hint lock\n");
			return NULL;
		}
	}

	skiplist -> list_head_locks = list_head_locks;

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

	skiplist -> level_ranges = level_ranges;

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



int level_range_cmp(const void * range_a, const void * range_b){

	float val = ((Level_Range *) range_a) -> start;
	float range_val_start = ((Level_Range *) range_b) -> start;
	float range_val_stop = ((Level_Range *) range_b) -> stop;

	if ((val >= range_val_start) && (val <= range_val_stop)){
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
	skiplist_item -> value_cnt = 1;


	ret = pthread_mutex_init(&(skiplist_item -> value_cnt_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init skiplist_item val cnt lock\n");
		return NULL;
	}

	// Determine level for item

	// between 0 and 1
	float rand_val = (float)rand() / (float)RAND_MAX;
	// now force between 0 and skiplist -> rand_level_upper_bound
	float rand_level_val = rand_val * skiplist -> rand_level_upper_bound;

	Level_Range dummy_range;
	dummy_range.start = rand_level_val;
	dummy_range.stop = rand_level_val;

	Level_Range * chosen_level = bsearch(&dummy_range, skiplist -> level_ranges, skiplist -> max_levels, sizeof(Level_Range), level_range_cmp);
	if (chosen_level == NULL){
		fprintf(stderr, "Error: could not find chosen level\n");
		return NULL;
	}

	uint8_t level = chosen_level -> level;

	skiplist_item -> level = level;

	ret = pthread_mutex_init(&(skiplist_item -> level_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init skiplist_item level lock\n");
		return NULL;
	}

	Skiplist_Item ** forward = (Skiplist_Item **) malloc((level + 1) * sizeof(Skiplist_Item *));
	if (forward == NULL){
		fprintf(stderr, "Error: malloc failed to allocate forward array for skiplist_item\n");
		return NULL;
	}
	for (uint8_t i = 0; i < level + 1; i++){
		forward[i] = NULL;
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

	skiplist_item -> is_deleted = false;

	return skiplist_item;
}

// This is called when there are no other ongoing operations, so we don't need to deal
// with locks
void destroy_skiplist_item(Skiplist_Item * skiplist_item){

	// assert(value_ctn == 0) === assert(get_count_deque(skiplist_item -> value_list) == 0)
	destroy_deque(skiplist_item -> value_list, false);

	// good practice to acquire lock before destroying
	pthread_mutex_destroy(&(skiplist_item -> value_cnt_lock));
	pthread_mutex_destroy(&(skiplist_item -> level_lock));

	free(skiplist_item -> forward);

	for (uint8_t i = 0; i < skiplist_item -> level + 1; i++){
		pthread_mutex_destroy(&((skiplist_item -> forward_locks)[i]));
	}

	free(skiplist_item -> forward_locks);

	free(skiplist_item);
}



// For a given starting item, key, and level
// Obtain the lock for the predecessor of where key would be inserted

// Skiplist_item is a hint about the previous location so without concurrency will be already correct
// and the while loops will terminate immediately

// Returns the rightmost item before key at a given level and locks it forward value at that level
Skiplist_Item * get_forward_lock(Skiplist * skiplist, Skiplist_Item * skiplist_item, void * key, uint8_t level){

	Skiplist_Item target_item;
	target_item.key = key;

	Skiplist_Item * cur_skiplist_item = skiplist_item;

	// if we passed in null, check if the the head of the list is non-null and smaller than key
	// (time might have passed between checking the previous at each level and trying to obtain lock)
	if ((cur_skiplist_item == NULL) && ((skiplist -> level_lists)[level] != NULL)
			&& ((skiplist -> key_cmp)(&target_item, (void *) ((skiplist -> level_lists)[level])) > 0)){
		cur_skiplist_item = (skiplist -> level_lists)[level];
	}
	
	// Continue search without locking to find next predecessor
	while (cur_skiplist_item != NULL && 
				((skiplist -> key_cmp)(&target_item, (void *) ((cur_skiplist_item -> forward)[level])) > 0)){
		cur_skiplist_item = (cur_skiplist_item -> forward)[level];
	}

	// if there is no predecessor that is smaller than key
	//	- either empty list or smallest element in list
	// thus we need to acquire the head lock at this level
	if (cur_skiplist_item == NULL){
		pthread_mutex_lock(&(skiplist -> list_head_locks)[level]);
		return NULL;
	}	

	// Now do the search while acquiring locks in case there were
	// inserts between loop advancing (94-96) and now
	// The loop got us as far as possible without requiring locking
	pthread_mutex_lock(&((cur_skiplist_item -> forward_locks)[level]));
	

	// We know there is at least 1 element in the list that is smaller than key now (due to check on line 101)
	// Also comparing key, NULL returns a positive value (null is greater than key), so we will not
	// advance the current skip list item to be null
	while ((skiplist -> key_cmp)(&target_item, (void *) ((cur_skiplist_item -> forward)[level])) > 0){

		// unlock the previous' forward lock
		pthread_mutex_unlock(&((cur_skiplist_item -> forward_locks)[level]));
		// we know this will be non-null because the key_cmp predicate would return false if comparing (key, NULL) > 0
		cur_skiplist_item = (cur_skiplist_item -> forward)[level];
		pthread_mutex_lock(&((cur_skiplist_item -> forward_locks)[level]));
	}

	// Now the rightmost element that is smaller than key is locked
	return cur_skiplist_item;
}


// Appends value to the skiplist_item -> value_list that matches key. If no key exists, creates a skiplist_item
// and intializes a deque with value
int insert_item_skiplist(Skiplist * skiplist, void * key, void * value) {


	Skiplist_Item target_item;
	target_item.key = key;

	// indicate that we are doing an operation
	// this is blocked during a clean
	pthread_mutex_lock(&(skiplist -> num_active_ops_lock));
	skiplist -> num_active_ops += 1;
	pthread_mutex_unlock(&(skiplist -> num_active_ops_lock));

	uint8_t max_levels = skiplist -> max_levels;
	void * prev_items_per_level[max_levels];
	for (uint8_t i = 0; i < max_levels; i++){
		prev_items_per_level[i] = NULL;
	}

	// use the level hint to quickly guess the correct starting level
	uint8_t cur_max_level_hint = skiplist -> cur_max_level_hint;

	int cur_max_level = (int) cur_max_level_hint;
	Skiplist_Item * cur_skiplist_item = (skiplist -> level_lists)[cur_max_level];
	

	// This part might not be needed...
	//	- keeping it here for readability and soundness
	// OK to be off with max level, just a performance detail not correctness
	// 	- (because all items are held within level 0)
	if (cur_skiplist_item == NULL){
		// hint was too high
		while ((cur_max_level > 0) && (cur_skiplist_item == NULL)){
			cur_max_level -= 1;
			cur_skiplist_item = (skiplist -> level_lists)[cur_max_level];
		}
	}
	else{
		// hint was too low
		while ((cur_max_level < max_levels) &&
					((skiplist -> level_lists[cur_max_level + 1]) != NULL)){
			cur_max_level += 1;
			cur_skiplist_item = (skiplist -> level_lists[cur_max_level]);
		}
	}
	

	// Search
	// find the rightmost elements at each level that are less than key
	// no locking needed
	for (int cur_level = cur_max_level; cur_level >= 0; cur_level--){

		// Start at the higher-level's maximum element less than key
		// 	- (which by construction also exists at lower levels)

		// if the there were no elements less than key at the previous level, 
		// start at the head (minimum) of this level
		if ((cur_skiplist_item != NULL) && 
			((skiplist -> key_cmp)(&target_item, (void *) (cur_skiplist_item)) < 0)){
			cur_skiplist_item = (skiplist -> level_lists)[cur_level];
		}

		// Advancing to the maximum element less than key at this level
		while (cur_skiplist_item != NULL && 
				((skiplist -> key_cmp)(&target_item, (void *) ((cur_skiplist_item -> forward)[cur_level])) > 0)) {
			
			// we know this will be non-null because the key_cmp predicate would return false if comparing (key, NULL) < 0 which is not > 0
			cur_skiplist_item = (cur_skiplist_item -> forward)[cur_level];
		}

		// set the previous item at this level
		// ensure that the current item is actually smaller

		// if there are no elements smaller at this level set prev to null
		if ((cur_skiplist_item != NULL) && 
			((skiplist -> key_cmp)(&target_item, (void *) (cur_skiplist_item)) < 0)){
			prev_items_per_level[cur_level] = NULL;
		}
		else{
			prev_items_per_level[cur_level] = cur_skiplist_item;
		}
		
		// decrease level and repeat
	}

	// Determine if this element is already in the skiplist
	// If it is in the skiplist then 
	
	// Use the unlocked search as guide for rightmost
	Skiplist_Item * rightmost_base_level_hint = prev_items_per_level[0];

	// Actually determine the current rightmost smaller than key and acquire lock
	Skiplist_Item * rightmost_base_level = get_forward_lock(skiplist, rightmost_base_level_hint, key, 0);

	int ret;


	Skiplist_Item * found_item;

	// if there are no smaller elements then possibly the inserted element is smallest
	// which is the head of the list
	if (rightmost_base_level == NULL){
		found_item = (skiplist -> level_lists)[0];
	}
	else{
		found_item = (rightmost_base_level -> forward)[0];
	}


	// if this item already exists, just add value to the deque 
	//	(which is thread safe)
	if (found_item != NULL &&
			((skiplist -> key_cmp)(&target_item, found_item) == 0)){

		pthread_mutex_lock(&(found_item -> level_lock));

		// now add the value to the value_list deque
		// can only fail on OOM 
		ret = insert_deque(found_item -> value_list, BACK_DEQUE, value);
		// upon successful insert
		if (ret == 0){
			pthread_mutex_lock(&(found_item -> value_cnt_lock));
			found_item -> value_cnt += 1;
			pthread_mutex_unlock(&(found_item -> value_cnt_lock));
		}

		if (rightmost_base_level == NULL){
			pthread_mutex_unlock(&((skiplist -> list_head_locks)[0]));
		}
		else{
			pthread_mutex_unlock(&((rightmost_base_level -> forward_locks)[0]));
		}
		

		// ensure to reduce active ops
		pthread_mutex_lock(&(skiplist -> num_active_ops_lock));
		skiplist -> num_active_ops -= 1;
		// can signal to gc condition variable in case the only
		// active op is the deleted tied to garbage collection
		if (skiplist -> num_active_ops == 0){
			pthread_cond_signal(&(skiplist -> gc_cv));
		}
		pthread_mutex_unlock(&(skiplist -> num_active_ops_lock));

		pthread_mutex_unlock(&(found_item -> level_lock));
		return ret;
	}


	// Otherwise we need to create a skiplist_item
	Skiplist_Item * new_item = init_skiplist_item(skiplist, key, value);
	if (new_item == NULL){
		fprintf(stderr, "Error: failure to init new skiplist_item\n");

		// ensure to reduce active ops
		pthread_mutex_lock(&(skiplist -> num_active_ops_lock));
		skiplist -> num_active_ops -= 1;
		// can signal to gc condition variable in case the only
		// active op is the deleted tied to garbage collection
		if (skiplist -> num_active_ops == 0){
			pthread_cond_signal(&(skiplist -> gc_cv));
		}
		pthread_mutex_unlock(&(skiplist -> num_active_ops_lock));

		return -1;
	}

	// now need to add this item!
	uint8_t item_level = new_item -> level;

	// start by locking the level at which the item is being inserted
	pthread_mutex_lock(&(new_item -> level_lock));

	// now insert y into all levels (possible exceeding cur_max_level)
	Skiplist_Item * rightmost_at_level = rightmost_base_level;
	for (uint8_t cur_insert_level = 0; cur_insert_level <= item_level; cur_insert_level++){

		// we already grabbed the forward lock for level 0 at line 276 above
		// use the prev items per level as a hint to advance to correct position to grab forward lock
		//	if this was null then get_forward_lock will confirm that the head of the list is null
		if (cur_insert_level != 0){
			rightmost_at_level = get_forward_lock(skiplist, prev_items_per_level[cur_insert_level], key, cur_insert_level);
		}

		// Insert after the rightmost before key
		if (rightmost_at_level != NULL){
			rightmost_at_level = rightmost_base_level;
			// this item's next value was the previous' next
			(new_item -> forward)[cur_insert_level] = (rightmost_at_level -> forward)[cur_insert_level];
			// the previous items next is now this
			(rightmost_at_level -> forward)[cur_insert_level] = new_item;
			// can unlock the rightmost before new element now
			pthread_mutex_unlock(&((rightmost_at_level -> forward_locks)[cur_insert_level]));
		}
		// if there were no elements smaller than we put this element at head of list
		else{
			// point to the previous head of the list (could be null)
			(new_item -> forward)[cur_insert_level] = (skiplist -> level_lists)[cur_insert_level];
			(skiplist -> level_lists)[cur_insert_level] = new_item;
			pthread_mutex_unlock(&((skiplist -> list_head_locks)[cur_insert_level]));
		}
	}
	// can unlock the level at which item was inserted now
	pthread_mutex_unlock(&(new_item -> level_lock));


	// Updating cur max level just leads to performance differences, not correctness!
	// 	- might be off by a few levels worst case but this is OK

	// determine if we need to modify the cur_max_level_hint to indicate that maximum non-null level

	// re-query the cur_max_level_hint (other concurrency might have occurred during processing of this function)
	cur_max_level_hint = skiplist -> cur_max_level_hint;
	cur_max_level = cur_max_level_hint;
	

	// If we are:
	//	a.) not at the max level, 
	// 	b.) there is an item at a level above the cur max level
	//	c.) We can acquire the level hint lock (no one else is currently updating)

	// Because we are inserting we can only increase the max level
	if ((cur_max_level < skiplist -> max_levels) &&
			((skiplist -> level_lists)[cur_max_level + 1] != NULL) &&
			(pthread_mutex_trylock(&(skiplist -> level_hint_lock)) == 0)){

		// while we are not at the cap and there is an item higher than us
		while ((cur_max_level < skiplist -> max_levels)
				&& ((skiplist -> level_lists)[cur_max_level + 1] != NULL)) {
			cur_max_level += 1;
		}

		// set new cur_max_level_hint
		skiplist -> cur_max_level_hint = cur_max_level;

		// can unlock now
		pthread_mutex_unlock(&(skiplist -> level_hint_lock));	
	}

	// ensure to reduce active ops
	//	in case we need to garbage collect and pause
	pthread_mutex_lock(&(skiplist -> num_active_ops_lock));
	skiplist -> num_active_ops -= 1;
	// can signal to gc condition variable in case the only
	// active op is the deleted tied to garbage collection
	if (skiplist -> num_active_ops == 0){
		pthread_cond_signal(&(skiplist -> gc_cv));
	}
	pthread_mutex_unlock(&(skiplist -> num_active_ops_lock));

	return 0;
}


// Removes element from skiplist_item -> value_list where the item is the minimum key >= key
// if no skiplist item (== value_list is NULL) then return NULL
void * take_closest_item_skiplist(Skiplist * skiplist, void * key) {

	Skiplist_Item target_item;
	target_item.key = key;

	pthread_mutex_lock(&(skiplist -> num_active_ops_lock));
	skiplist -> num_active_ops += 1;
	pthread_mutex_unlock(&(skiplist -> num_active_ops_lock));

	uint8_t max_levels = skiplist -> max_levels;
	void * prev_items_per_level[max_levels];
	for (uint8_t i = 0; i < max_levels; i++){
		prev_items_per_level[i] = NULL;
	}

	// use the level hint to quickly guess the correct starting level
	uint8_t cur_max_level_hint = skiplist -> cur_max_level_hint;

	int cur_max_level = (int) cur_max_level_hint;
	Skiplist_Item * cur_skiplist_item = (skiplist -> level_lists)[cur_max_level];
	

	// This part might not be needed...
	//	- keeping it here for readability and soundness
	// OK to be off with max level, just a performance detail not correctness
	// 	- (because all items are held within level 0)
	if (cur_skiplist_item == NULL){
		// hint was too high
		while ((cur_max_level > 0) && (cur_skiplist_item == NULL)){
			cur_max_level -= 1;
			cur_skiplist_item = (skiplist -> level_lists)[cur_max_level];
		}
	}
	else{
		// hint was too low
		while ((cur_max_level < max_levels) &&
					((skiplist -> level_lists[cur_max_level + 1]) != NULL)){
			cur_max_level += 1;
			cur_skiplist_item = (skiplist -> level_lists[cur_max_level]);
		}
	}
	

	

	// Search
	// find the rightmost elements at each level that are less than key
	// no locking needed
	for (int cur_level = cur_max_level; cur_level >= 0; cur_level--){

		// Start at the higher-level's maximum element less than key
		// 	- (which by construction also exists at lower levels)

		// if the there were no elements less than key at the previous level, 
		// start at the head (minimum) of this level
		if ((cur_skiplist_item != NULL) && 
			((skiplist -> key_cmp)(&target_item, (void *) (cur_skiplist_item)) < 0)){
			cur_skiplist_item = (skiplist -> level_lists)[cur_level];
		}

		// Advancing to the maximum element less than key at this level
		while (cur_skiplist_item != NULL && 
				((skiplist -> key_cmp)(&target_item, (void *) ((cur_skiplist_item -> forward)[cur_level])) > 0)) {
			// we know this will be non-null because the key_cmp predicate would return false if comparing (key, NULL) < 0 which is not > 0
			cur_skiplist_item = (cur_skiplist_item -> forward)[cur_level];
		}

		// set the previous item at this level
		// ensure that the current item is actually smaller

		// if there are no elements smaller at this level set prev to null
		if ((cur_skiplist_item != NULL) && 
			((skiplist -> key_cmp)(&target_item, (void *) (cur_skiplist_item)) < 0)){
			prev_items_per_level[cur_level] = NULL;
		}
		else{
			prev_items_per_level[cur_level] = cur_skiplist_item;
		}

		// decrease level and repeat
	}


	// however need to be careful about advancing past garabge
	Skiplist_Item * closest_item;

	// this only occurs when the list is empty
	// if the key is the smallest value then the rightmost_base_level will point to the 
	Skiplist_Item * rightmost_base_level = prev_items_per_level[0];

	
	// there are no elements smaller, so we want to take the from 
	// the minimum element in the list (which is the head of level-0)
	// if key exists then we know that the minimum will be the key, so
	// this sensisble
	if (rightmost_base_level == NULL){
		closest_item = (skiplist -> level_lists)[0];
	}
	// otherwise we want to take the next element of predecessor at level 0 (which is either
	// search key if it exsits, or the smallest key that is greater than search key)
	else{
		closest_item = (rightmost_base_level -> forward)[0];
	}


	// skip past elements with 0 values in their deque (returns -1)
	//		- these


	// THE ACTUAL VALUE TO RETURNED
	// EXTRACTED FROM TAKE_DEQUE!
	void * val = NULL;


	bool to_delete_skiplist_item = false;
	while (closest_item != NULL)  {
		pthread_mutex_lock(&(closest_item -> value_cnt_lock));
		// If closest_item is in GC-bin but not yet deleted
		if (closest_item -> value_cnt == 0){
			pthread_mutex_unlock(&(closest_item -> value_cnt_lock));
			closest_item = (closest_item -> forward)[0];
		}
		else{
			// guaranteed to succeed because we hold lock for value count
			// and we know > 0 items
			take_deque(closest_item -> value_list, FRONT_DEQUE, &val);
			closest_item -> value_cnt -= 1;
			if (closest_item -> value_cnt == 0){
				to_delete_skiplist_item = true;
			}
			pthread_mutex_unlock(&(closest_item -> value_cnt_lock));
			break;
		}
	}

	// If we didn't take a value, or we took a value but there are still
	// more values tied to a specific key (so we don't need to delete)
	if (!to_delete_skiplist_item){
		pthread_mutex_lock(&(skiplist -> num_active_ops_lock));
		skiplist -> num_active_ops -= 1;
		// can signal to gc condition variable in case the only
		// active op is the deleted tied to garbage collection
		if (skiplist -> num_active_ops == 0){
			pthread_cond_signal(&(skiplist -> gc_cv));
		}
		pthread_mutex_unlock(&(skiplist -> num_active_ops_lock));
		return val;
	}


	// Other node's cannot delete while this is being deleted
	pthread_mutex_lock(&(closest_item -> level_lock));

	// cannot delete an already deleted item
	if (closest_item -> is_deleted){
		pthread_mutex_unlock(&(closest_item -> level_lock));
		pthread_mutex_lock(&(skiplist -> num_active_ops_lock));
		skiplist -> num_active_ops -= 1;
		// can signal to gc condition variable in case the only
		// active op is the deleted tied to garbage collection
		if (skiplist -> num_active_ops == 0){
			pthread_cond_signal(&(skiplist -> gc_cv));
		}
		pthread_mutex_unlock(&(skiplist -> num_active_ops_lock));
		return val;
	}


	// Now we have removed the last value in deque, so we need to
	// reassign pointers and add to gc bin

	Skiplist_Item * prev_at_level;
	
	int closest_item_level = (int) closest_item -> level;
	void * closest_item_key = closest_item -> key;
	for (int cur_delete_level = closest_item_level; cur_delete_level >= 0; cur_delete_level--){

		prev_at_level = get_forward_lock(skiplist, prev_items_per_level[cur_delete_level], closest_item_key, cur_delete_level);

		// lock this item's forward pointer so others can't go through it while it is being updated
		pthread_mutex_lock(&((closest_item -> forward_locks)[cur_delete_level]));

		// if there were no items smaller then that means this was the head of the list
		// and so we should set the head of the list to be this item's next
		if (prev_at_level == NULL){
			(skiplist -> level_lists)[cur_delete_level] = (closest_item -> forward)[cur_delete_level];
		}
		else{
			(prev_at_level -> forward)[cur_delete_level] = (closest_item -> forward)[cur_delete_level];
		}


		// Enable passing through y by swapping with x
		(closest_item -> forward)[cur_delete_level] = prev_at_level;

		if (prev_at_level == NULL){
			pthread_mutex_unlock(&((skiplist -> list_head_locks)[cur_delete_level]));
		}
		else{
			pthread_mutex_unlock(&((prev_at_level -> forward_locks)[cur_delete_level]));
		}

		pthread_mutex_unlock(&((closest_item -> forward_locks)[cur_delete_level]));
	}


	closest_item -> is_deleted = true;

	pthread_mutex_lock(&(skiplist -> gc_cnt_lock));
	uint64_t cur_gc_cnt = skiplist -> cur_gc_cnt;
	(skiplist -> delete_bin)[cur_gc_cnt] = closest_item;
	skiplist -> cur_gc_cnt = cur_gc_cnt + 1;

	// got placed in garbage bin so we can unlock 
	pthread_mutex_unlock(&(closest_item -> level_lock));


	

	// we need to clean out the bin
	// this means waiting for all ongoing operations to complete
	if (skiplist -> cur_gc_cnt == skiplist -> gc_cap){

		pthread_mutex_lock(&(skiplist -> num_active_ops_lock));

		// count self as done
		skiplist -> num_active_ops -= 1;
		
		while(skiplist -> num_active_ops > 0){
			pthread_cond_wait(&(skiplist -> gc_cv), &(skiplist -> num_active_ops_lock));
		}

		// now no ongoing operations so we can clear out the garbage bin
		
		for (uint64_t i = 0; i < skiplist -> gc_cap; i++){
			destroy_skiplist_item((Skiplist_Item *) ((skiplist -> delete_bin)[i]));
			(skiplist -> delete_bin)[i] = NULL;
		}

		// reset gc count back to 0
		skiplist -> cur_gc_cnt = 0;

		// now the operations can continue that were blocked at the beginning
		pthread_mutex_unlock(&(skiplist -> num_active_ops_lock));
	}
	else{
		pthread_mutex_lock(&(skiplist -> num_active_ops_lock));
		skiplist -> num_active_ops -= 1;
		if (skiplist -> num_active_ops == 0){
			pthread_cond_signal(&(skiplist -> gc_cv));
		}
		pthread_mutex_unlock(&(skiplist -> num_active_ops_lock));
	}

	pthread_mutex_unlock(&(skiplist -> gc_cnt_lock));
	


	// now potentially decrease level int
	cur_max_level_hint = skiplist -> cur_max_level_hint;
	cur_max_level = cur_max_level_hint;

	if ((cur_max_level > 0) 
		&& ((skiplist -> level_lists)[cur_max_level] == NULL) 
		&& (pthread_mutex_trylock(&(skiplist -> level_hint_lock)) == 0)){

		while ((cur_max_level > 0) && ((skiplist -> level_lists)[cur_max_level] == NULL)){
			cur_max_level -= 1;
		}

		skiplist -> cur_max_level_hint = cur_max_level;

		pthread_mutex_unlock(&(skiplist -> level_hint_lock));
	}

	return val;
}