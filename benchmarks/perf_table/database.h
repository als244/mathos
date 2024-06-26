#ifndef DATABASE_H
#define DATABASE_H

#include "common.h"

#include <ndbm.h>
#include <sys/stat.h>        /* For S_* mode constants */
#include <fcntl.h>           /* For O_* constants */


// lengths do not included null terminator, but total_data_bytes does
typedef struct record_header {
        size_t total_data_bytes;
        size_t text_len;
        size_t url_len;
        size_t dump_len;
} RecordHeader;


// Data initially as 1 byte to make compiler work, however it's contents
// are sized according to the record header
typedef struct record {
        RecordHeader header;
        char data[1];
} Record;

Record * fetch_record(DBM * db, uint64_t id);
int fetch_record_text(DBM * db, uint64_t id, char ** ret_text, uint64_t * ret_len);
DBM * open_db(char * path);

#endif