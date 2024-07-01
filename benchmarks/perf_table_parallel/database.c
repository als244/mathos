#include "database.h"

Record * fetch_record(DBM * db, uint64_t id) {

        char * key_str;
        asprintf(&key_str, "%ld", id);

        datum key_datum;
        key_datum.dptr = key_str;
        key_datum.dsize = strlen(key_str);


        datum data_datum = dbm_fetch(db, key_datum);
        free(key_str);
        if (data_datum.dptr == NULL) {
                fprintf(stderr, "Error: could not find record with id = %ld\n", id);
                return NULL;
        }

        // data pointer referring to value in DBM library so copy into this user program
        Record * record = malloc(data_datum.dsize);
        memcpy(record, data_datum.dptr, data_datum.dsize);

        return record;
}

int fetch_record_text(DBM * db, uint64_t id, char ** ret_text, uint64_t * ret_len) {

		Record * record = fetch_record(db, id);
		if (record == NULL){
			fprintf(stderr, "Error: could not fetch record with id: %lu", id);
			return -1;
		}

		uint64_t text_len = record -> header.text_len;
		*ret_len = text_len;
		char * text = malloc(text_len + 1);
		char * text_start = record -> data;
		strcpy(text, text_start);
		*ret_text = text;
		free(record);
		return 0;
}

DBM * open_db(char * path){
	
	DBM * dbm_ptr;
	dbm_ptr = dbm_open(path, O_RDONLY, S_IRUSR);
	if (dbm_ptr == NULL) {
		fprintf(stderr, "Error: could not open database with path %s\n", path);
	}
	return dbm_ptr;
}