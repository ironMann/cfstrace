#ifndef _SQLITE_ADAPTER_H
#define _SQLITE_ADAPTER_H

#include "sqlite3.h"

typedef struct sqlite_adapter {
	sqlite3 *db;
	char db_name[1024];

	//prepared statements for insert
	sqlite3_stmt *open_insert_stmt;
	sqlite3_stmt *close_insert_stmt;
	sqlite3_stmt *read_insert_stmt;
	sqlite3_stmt *write_insert_stmt;

	//transaction thread ?!
	pthread_t tthread;
	int should_commit;
} sqlite_adapter_t;

sqlite_adapter_t* open_database(const char *db_file_name);
void              close_database(sqlite_adapter_t *adapter);
void              insert_data(sqlite_adapter_t *adapter, const char *hostname, void *operation);


#endif
