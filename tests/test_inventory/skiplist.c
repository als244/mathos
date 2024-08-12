#include "skiplist.h"


Skiplist * init_skiplist(Item_Cmp key_cmp, Item_Cmp val_cmp, uint8_t max_levels, float level_factor, uint64_t min_items_to_check_reap, float max_zombie_ratio) {

	Skiplist * skiplist = (Skiplist *) malloc(sizeof(Skiplist));
	if (skiplist == NULL){
		fprintf(stderr, "Error: malloc failed to allocate skiplist\n");
		return NULL;
	}

	skiplist -> max_levels = max_levels;
	skiplist -> level_factor = level_factor;
	skiplist -> key_cmp = key_cmp;
	skiplist -> val_cmp = val_cmp;


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

	ret = pthread_mutex_init(&(skiplist -> cnt_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init gc cnt lock\n");
		return NULL;
	}

	skiplist -> total_item_cnt = 0;
	skiplist -> min_items_to_check_reap = min_items_to_check_reap;
	skiplist -> max_zombie_ratio = max_zombie_ratio;
	skiplist -> zombie_cnt = 0;
	skiplist -> is_reaping = false;
	skiplist -> at_zombie_cap = false;

	ret = pthread_cond_init(&(skiplist -> reaping_cv), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init gc_cv\n");
	}

	Deque * zombies = init_deque(NULL);
	skiplist -> zombies = zombies;

	skiplist -> num_active_ops = 0;
	ret = pthread_mutex_init(&(skiplist ->  op_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init num_active_ops lock\n");
		return NULL;
	}

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

	// Can use the skiplist -> val_cmp to retrieve a specific
	// value from this deque. If null then will just take the front value
	Deque * value_list = init_deque(skiplist -> val_cmp);

	int ret = insert_lockless_deque(value_list, BACK_DEQUE, value);
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

	// for readability
	//	- makes clear the zombie aspects seperate from value_cnt (even though they are the same)
	skiplist_item -> is_zombie = false;

	return skiplist_item;
}

// This is called when there are no other ongoing operations, so we don't need to deal
// with locks
void destroy_skiplist_item(Skiplist_Item * skiplist_item){

	// assert(value_cntn == 0) === assert(get_count_deque(skiplist_item -> value_list) == 0)
	destroy_deque(skiplist_item -> value_list, false);

	// good practice to acquire lock before destroying
	pthread_mutex_destroy(&(skiplist_item -> value_cnt_lock));

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

	// Cannot insert during a reap
	pthread_mutex_lock(&(skiplist -> op_lock));
	while (skiplist -> is_reaping) {
		pthread_cond_wait(&(skiplist -> reaping_cv), &(skiplist -> op_lock));
	}

	// The reapling completed so increment active ops and release lock
	skiplist -> num_active_ops += 1;
	pthread_mutex_unlock(&(skiplist -> op_lock));

	uint8_t max_levels = skiplist -> max_levels;

	// we know max levels < 256, so no stack overflow here...
	void * prev_items_per_level[max_levels];
	memset(prev_items_per_level, 0, max_levels * sizeof(void *));

	// use the level hint to quickly guess the correct starting level
	uint8_t cur_max_level_hint = skiplist -> cur_max_level_hint;

	int cur_max_level = (int) cur_max_level_hint;
	Skiplist_Item * cur_skiplist_item = (skiplist -> level_lists)[cur_max_level];
	
	// Search
	// find the rightmost elements at each level that are less than key
	// no locking needed
	for (int cur_level = cur_max_level; cur_level >= 0; cur_level--){

		// Start at the higher-level's maximum element less than key
		// 	- (which by construction also exists at lower levels)

		// if the there were no elements less than key at the previous level, 
		// start at the head (minimum) of this level
		if ((cur_skiplist_item != NULL) && 
			((skiplist -> key_cmp)(&target_item, (void *) (cur_skiplist_item)) <= 0)){
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
			((skiplist -> key_cmp)(&target_item, (void *) (cur_skiplist_item)) <= 0)){
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
	Skiplist_Item * rightmost_pred_base_level_hint = prev_items_per_level[0];

	// Actually determine the current rightmost smaller than key and acquire lock
	// (other inserts may have occurred concurrently)
	
	// The prevents a duplicate skiplist_item (with the same key) from being inserted
	// because the first insert will not release this lock until after it has linked, 
	// to it's pred (which would be the same as this for a concurrent insert)
	//	=> meaning that when the 2nd insert comes this pred's forward will match
	Skiplist_Item * rightmost_pred_base_level = get_forward_lock(skiplist, rightmost_pred_base_level_hint, key, 0);

	int ret;


	Skiplist_Item * found_item;

	// if there are no smaller elements then possibly the inserted element is smallest
	// which is the head of the list
	if (rightmost_pred_base_level == NULL){
		found_item = (skiplist -> level_lists)[0];
	}
	else{
		found_item = (rightmost_pred_base_level -> forward)[0];
	}

	

	// if this item already exists, just add value to the deque 
	//	(which is thread safe)
	if ((found_item != NULL) && ((skiplist -> key_cmp)(&target_item, found_item) == 0)){

		// Prevent concurrent insertations that alter the global zombie count
		// incorrectly.
		pthread_mutex_lock(&(found_item -> value_cnt_lock));

		// Determine if this item was a zombie that is being revived 
		if (found_item -> value_cnt == 0){

			// Decrease the zombie count
			pthread_mutex_lock(&(skiplist -> cnt_lock));
			skiplist -> zombie_cnt -= 1;

			// Set the value in the zombies deque to NULL
			// so this will not be deleted
			found_item -> is_zombie = false;
			pthread_mutex_unlock(&(skiplist -> cnt_lock));
		}

		// now add the value to the value_list deque
		// we are holding the value count lock so can use lockless variant
		// can only fail on OOM 
		ret = insert_lockless_deque(found_item -> value_list, BACK_DEQUE, value);
		// upon successful insert
		if (ret == 0){
			found_item -> value_cnt += 1;
		}

		if (rightmost_pred_base_level == NULL){
			pthread_mutex_unlock(&((skiplist -> list_head_locks)[0]));
		}
		else{
			pthread_mutex_unlock(&((rightmost_pred_base_level -> forward_locks)[0]));
		}
		
		// ensure to reduce active ops
		pthread_mutex_lock(&(skiplist -> op_lock));
		skiplist -> num_active_ops -= 1;
		// if this was the last pending operation before reap can occur
		// the remaining active op is the reap thread
		if ((skiplist -> num_active_ops == 1) && (skiplist -> at_zombie_cap)) {
			pthread_cond_signal(&(skiplist -> reaping_cv));
		}
		pthread_mutex_unlock(&(skiplist -> op_lock));

		pthread_mutex_unlock(&(found_item -> value_cnt_lock));
		return ret;
	}

	// Key not found, so we need to create a new item

	Skiplist_Item * new_item = init_skiplist_item(skiplist, key, value);
	if (new_item == NULL){
		fprintf(stderr, "Error: failure to init new skiplist_item\n");

		// ensure to reduce active ops
		pthread_mutex_lock(&(skiplist -> op_lock));
		skiplist -> num_active_ops -= 1;
		// if this was the last pending operation before reap can occur
		// the remaining active op is the reap thread
		if ((skiplist -> num_active_ops == 1) && (skiplist -> at_zombie_cap)) {
			pthread_cond_signal(&(skiplist -> reaping_cv));
		}
		pthread_mutex_unlock(&(skiplist -> op_lock));

		return -1;
	}

	// now need to add this item!
	uint8_t item_level = new_item -> level;

	// Now insert new item into all its levels 
	//	(possibly exceeding cur_max_level, but that is OK because we
	//	intialized prev_items_per_level to NULL for all levels)
	Skiplist_Item * rightmost_pred_at_level;
	for (uint8_t cur_insert_level = 0; cur_insert_level <= item_level; cur_insert_level++){

		// we already grabbed the forward lock for level 0 at line 276 above
		// use the prev items per level as a hint to advance to correct position to grab forward lock
		// if this was null then get_forward_lock will confirm that the head of the list is null and
		// it will grab the head lock assoicated with this level
		if (cur_insert_level != 0){
			rightmost_pred_at_level = get_forward_lock(skiplist, prev_items_per_level[cur_insert_level], key, cur_insert_level);
		}
		else{
			// we already grabbed the lock for this before checking if the item was already in skiplist
			rightmost_pred_at_level = rightmost_pred_base_level;
		}

		// If there was an item with a smaller key, then insert in between
		if (rightmost_pred_at_level != NULL){
			// this item's next value was the previous' next
			(new_item -> forward)[cur_insert_level] = (rightmost_pred_at_level -> forward)[cur_insert_level];
			// the previous items next is now this
			(rightmost_pred_at_level -> forward)[cur_insert_level] = new_item;
			// can unlock the rightmost before new element now
			pthread_mutex_unlock(&((rightmost_pred_at_level -> forward_locks)[cur_insert_level]));
		}
		// if there were no elements smaller than we put this element at head of list
		else{
			// point to the previous head of the list (could be null)
			(new_item -> forward)[cur_insert_level] = (skiplist -> level_lists)[cur_insert_level];
			(skiplist -> level_lists)[cur_insert_level] = new_item;
			// get_forward_lock takes a list head lock if there are no smaller elements
			pthread_mutex_unlock(&((skiplist -> list_head_locks)[cur_insert_level]));
		}
	}

	// update the total cnt because we inserted a new item
	pthread_mutex_lock(&(skiplist -> cnt_lock));
	skiplist -> total_item_cnt += 1;
	pthread_mutex_unlock(&(skiplist -> cnt_lock));


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
	pthread_mutex_lock(&(skiplist -> op_lock));
	skiplist -> num_active_ops -= 1;
	// if this was the last pending operation before reap can occur
	// the remaining active op is the reap thread
	if ((skiplist -> num_active_ops == 1) && (skiplist -> at_zombie_cap)) {
		pthread_cond_signal(&(skiplist -> reaping_cv));
	}
	pthread_mutex_unlock(&(skiplist -> op_lock));

	return 0;
}


// NOTE: This reap function will run when no other operations are occurring
// and it is single threaded. Thus no need to acquire any locks

// This will unlink the item and then free it's memory
void reap_skiplist_item(Skiplist * skiplist, Skiplist_Item * zombie){

	Skiplist_Item target_item;
	target_item.key = zombie -> key;

	uint8_t zombie_level = zombie -> level;
	// we know max levels < 256, so no stack overflow here...
	void * prev_items_per_level[zombie_level];
	memset(prev_items_per_level, 0, zombie_level * sizeof(void *));


	Skiplist_Item * cur_skiplist_item = (skiplist -> level_lists)[zombie_level];
	

	// Search
	// find the rightmost elements at each level that are less than key
	// no locking needed
	for (int cur_level = zombie_level; cur_level >= 0; cur_level--){

		// Start at the higher-level's maximum element less than key
		// 	- (which by construction also exists at lower levels)

		// if the there were no elements less than key at the previous level, 
		// start at the head (minimum) of this level
		if ((cur_skiplist_item != NULL) && 
			((skiplist -> key_cmp)(&target_item, (void *) (cur_skiplist_item)) <= 0)){
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
			((skiplist -> key_cmp)(&target_item, (void *) (cur_skiplist_item)) <= 0)){
			prev_items_per_level[cur_level] = NULL;
		}
		else{
			prev_items_per_level[cur_level] = cur_skiplist_item;
		}

		// decrease level and repeat
	}


	// Now we need to reset the forward link for predecessors

	// Because this function stops the world and is single-threaded
	// we don't need to acquire any locks
	Skiplist_Item * prev_at_level;
	for (int cur_delete_level = zombie_level; cur_delete_level >= 0; cur_delete_level--){

		prev_at_level = prev_items_per_level[cur_delete_level];
		// if there were no items smaller then that means this was the head of the list
		// and so we should set the head of the list to be this item's next
		if (prev_at_level == NULL){
			(skiplist -> level_lists)[cur_delete_level] = (zombie -> forward)[cur_delete_level];
		}
		else{
			(prev_at_level -> forward)[cur_delete_level] = (zombie -> forward)[cur_delete_level];
		}
	}


	// Now we can destory the skiplist item and free up memory
	destroy_skiplist_item(zombie);

	return;
}



// Removes an item from a skiplist_item -> value_list deque

// The skiplist take_type determines what skiplist_item to try and take from relative to key

// If val_match is NULL, the first item will be removed. If it is non-null, then will take the first item
// to match based on skiplist -> val_cmp. When val_match is set, its purpose is for removal (not search)

// Search val is secondary to the take type (meaning only removes the value from a given key), thus
// will almost always be used with EQ_SKIPLIST type. If val_cmp was initialized as NULL, then search val has no effect
void * take_item_skiplist(Skiplist * skiplist, SkiplistTakeType take_type, void * key, void * search_val) {

	// Cannot insert during a reap
	pthread_mutex_lock(&(skiplist -> op_lock));
	while (skiplist -> is_reaping) {
		pthread_cond_wait(&(skiplist -> reaping_cv), &(skiplist -> op_lock));
	}

	// The reapling completed so increment active ops and release lock
	skiplist -> num_active_ops += 1;
	pthread_mutex_unlock(&(skiplist -> op_lock));

	Skiplist_Item target_item;
	target_item.key = key;


	// use the level hint to quickly guess the correct starting level
	uint8_t cur_max_level_hint = skiplist -> cur_max_level_hint;

	int cur_max_level = (int) cur_max_level_hint;
	Skiplist_Item * cur_skiplist_item = (skiplist -> level_lists)[cur_max_level];

	// Search
	
	// TODO: for take_type == MIN_SKIPLIST, can skip the search
	//	ALSO TODO: add tail pointer to track maximum element


	// find the rightmost elements at each level that are less than key
	// no locking needed
	for (int cur_level = cur_max_level; cur_level >= 0; cur_level--){

		// Start at the higher-level's maximum element less than key
		// 	- (which by construction also exists at lower levels)

		// if the there were no elements less than key at the previous level, 
		// start at the head (minimum) of this level
		if ((cur_skiplist_item != NULL) && 
			((skiplist -> key_cmp)(&target_item, (void *) (cur_skiplist_item)) <= 0)){
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
			((skiplist -> key_cmp)(&target_item, (void *) (cur_skiplist_item)) <= 0)){
			cur_skiplist_item = NULL;
		}

		// decrease level and repeat
	}


	// however need to be careful about advancing past garabge
	Skiplist_Item * closest_item;

	// this only occurs when the list is empty
	// if the key is the smallest value then the rightmost_base_level will point to the 
	Skiplist_Item * rightmost_pred_base_level = cur_skiplist_item;

	
	// there are no elements smaller, so we want to take the from 
	// the minimum element in the list (which is the head of level-0)
	// if key exists then we know that the minimum will be the key, so
	// this sensisble
	if (rightmost_pred_base_level == NULL){
		closest_item = (skiplist -> level_lists)[0];
	}
	// otherwise we want to take the next element of predecessor at level 0 (which is either
	// search key if it exsits, or the smallest key that is greater than search key)
	else{
		closest_item = (rightmost_pred_base_level -> forward)[0];
	}




	// THE ACTUAL VALUE TO RETURNED
	// EXTRACTED FROM TAKE_DEQUE!
	void * val = NULL;
	bool to_create_zombie = false;

	// if the take type is equal then closest_item needs to equal key
	// otherwise not found
	if (take_type == EQ_SKIPLIST && 
			((skiplist -> key_cmp)(&target_item, closest_item) == 0)){
		pthread_mutex_lock(&(closest_item -> value_cnt_lock));
		// only try taking if there are items
		if (closest_item -> value_cnt != 0){
			// take any item
			if ((search_val == NULL) || (skiplist -> val_cmp == NULL)){
				// guaranteed to succeed because we hold lock for value count
				// and we know > 0 items
				take_lockless_deque(closest_item -> value_list, FRONT_DEQUE, &val);
				closest_item -> value_cnt -= 1;
				if (closest_item -> value_cnt == 0){
					to_create_zombie = true;
					closest_item -> is_zombie = true;
				}
			}
			// only taking first matching item
			else{
				// returns 0 on success, -1 if failure
				// the comparison within deque list be using skiplist -> val_cmp
				if (take_first_matching_lockless_deque(closest_item -> value_list, FRONT_DEQUE, search_val, &val) == 0){
					closest_item -> value_cnt -= 1;
					if (closest_item -> value_cnt == 0){
						to_create_zombie = true;
						closest_item -> is_zombie = true;
					}
				}
			}
		}
		pthread_mutex_unlock(&(closest_item -> value_cnt_lock));
	}
	// skip past elements with 0 values in their deque
	// If we remove from a skiplist that still has at least
	// 1 value then we can immediately return without 
	// changing anything about skiplist
	else if (take_type == GREATER_OR_EQ_SKIPLIST){
		while (closest_item != NULL)  {
			pthread_mutex_lock(&(closest_item -> value_cnt_lock));
			// If closest_item is in zombie deque but not yet deleted
			if (closest_item -> value_cnt == 0){
				pthread_mutex_unlock(&(closest_item -> value_cnt_lock));
				closest_item = (closest_item -> forward)[0];
			}
			// There are values within this skiplist_item
			else{
				// take any item
				if ((search_val == NULL) || (skiplist -> val_cmp == NULL)){
					// guaranteed to succeed because we hold lock for value count
					// and we know > 0 items
					take_lockless_deque(closest_item -> value_list, FRONT_DEQUE, &val);
					closest_item -> value_cnt -= 1;
					if (closest_item -> value_cnt == 0){
						to_create_zombie = true;
						closest_item -> is_zombie = true;
					}
				}
				// only taking first matching item
				else{
					// returns 0 on success, -1 if failure
					// the comparison within deque list be using skiplist -> val_cmp
					if (take_first_matching_lockless_deque(closest_item -> value_list, FRONT_DEQUE, search_val, &val) == 0){
						closest_item -> value_cnt -= 1;
						if (closest_item -> value_cnt == 0){
							to_create_zombie = true;
							closest_item -> is_zombie = true;
						}
					}
				}
				pthread_mutex_unlock(&(closest_item -> value_cnt_lock));
				break;
			}
		}
	}

	// If we didn't take a value (no matching items), or we took a value but there are still
	// more values tied to a specific key (so we don't need to create zombie)
	if (!to_create_zombie){
		// ensure to reduce active ops
		pthread_mutex_lock(&(skiplist -> op_lock));
		skiplist -> num_active_ops -= 1;
		// if this was the last pending operation before reap can occur
		// the remaining active op is the reap thread
		if ((skiplist -> num_active_ops == 1) && (skiplist -> at_zombie_cap)) {
			pthread_cond_signal(&(skiplist -> reaping_cv));
		}
		pthread_mutex_unlock(&(skiplist -> op_lock));
		return val;
	}


	// Now we will create a zombie and see if this triggers a reap

	// indicator if this thread will be reaping
	bool to_reap = false;

	// if two threads reached this point (on the boundary of a reaping condition)
	// and there is a race, we want one thread to complete adding a zombie and decrement it's 
	// active op (without checking if it needs to reap), and the other to wait until there are no more active ops

	// In the case there is a reap, we will use the cnt lock
	// as a serialization point
	pthread_mutex_lock(&(skiplist -> cnt_lock));

	skiplist -> zombie_cnt += 1;

	// Initiate a reap if:
	//	a.) The total number of items is high enough to warrant a potential reap
	//	b.) The number of zombies exceeds the ratio
	// 	c.) Another thread hasn't started to intiate a reap
	if ((skiplist -> total_item_cnt >= skiplist -> min_items_to_check_reap) &&
		(skiplist -> zombie_cnt > skiplist -> total_item_cnt * skiplist -> max_zombie_ratio) &&
		!(skiplist -> is_reaping)){
		// set this thread to be responsible for waiting for all other operations to complete
		// and then reap
		to_reap = true;
		// Now other operations will not be able to begin
		skiplist -> is_reaping = true;
	}

	pthread_mutex_unlock(&(skiplist -> cnt_lock));

	Deque * zombies = skiplist -> zombies;

	// Insert the item into the zombie deque
	Skiplist_Item * zombie = closest_item;
	// only errors on OOM
	// inserting the deque is thread safe
	int ret = insert_deque(zombies, BACK_DEQUE, zombie);
	if (ret != 0){
		fprintf(stderr, "Error: inserting zombie into deque failed\n");
		return NULL;
	}


	// if we are reaping then we need to wait for all other operations
	// to complete
	if (to_reap){

		// set the at_zombie_cap = true under protection of op lock
		// so then when the other operations complete and acquire the 
		// lock they will see if they need to signal this thread
		pthread_mutex_lock(&(skiplist -> op_lock));
		skiplist -> at_zombie_cap = true;

		while(skiplist -> num_active_ops > 1){
			pthread_cond_wait(&(skiplist -> reaping_cv), &(skiplist -> op_lock));
		}

		// we are the only operation and all others are blocked, so we can begin
		// reaping without worrying about locks

		Skiplist_Item * zombie_to_reap;


		while (zombies -> head != NULL){
			// shouldn't be an error because this thread has exclusive access to this deque
			// (no other ops are ongoing)
			take_lockless_deque(zombies, FRONT_DEQUE, (void **) &zombie_to_reap);

			// confirm that the zombie was not revived
			if (!(zombie_to_reap -> is_zombie)){
				// assert(value_cnt == 0) === get_count_deque(zombie -> value_list == 0)
				continue;
			}

			// otherwise call the reap function which will unlink this skiplist_item
			// from all lists and free the memory for zombie
			reap_skiplist_item(skiplist, zombie);

			skiplist -> total_item_cnt -= 1;
			skiplist -> zombie_cnt -= 1;
		}

		// assert(skiplist -> zombie_cnt == 0)

		// we will hold on to the op_lock until after we update the 
		// the max level count and will then broadcast to the reaping_cv
		// there we are not reaping anymore
		skiplist -> is_reaping = false;
		skiplist -> at_zombie_cap = false;
	}
	
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


	// if this thread did reaping
	if (to_reap){
		// we are already holding on to the op lock in this case
		skiplist -> num_active_ops -= 1;
		// assert (skiplist -> num_active_ops == 0)
		pthread_mutex_unlock(&(skiplist -> op_lock));
		// if there were functions that were blocked during the reap
		// now skiplist -> is_reaping has been set to false, so when
		// these threads wake up they can advance
		pthread_cond_broadcast(&(skiplist -> reaping_cv));
	}
	else{
		// ensure to reduce active ops
		pthread_mutex_lock(&(skiplist -> op_lock));
		skiplist -> num_active_ops -= 1;
		// if this was the last pending operation before reap can occur
		// the remaining active op is the reap thread
		if ((skiplist -> num_active_ops == 1) && (skiplist -> at_zombie_cap)) {
			pthread_cond_signal(&(skiplist -> reaping_cv));
		}
		pthread_mutex_unlock(&(skiplist -> op_lock));
	}

	return val;
}


