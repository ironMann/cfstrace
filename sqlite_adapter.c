#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "sqlite3.h"
#include "cfstrace.h"
#include "sqlite_adapter.h"

#define create_open "CREATE TABLE IF NOT EXISTS open_table ( hostname TEXT NOT NULL, timestamp INTEGER NOT NULL, pid INTEGER NOT NULL, tid INTEGER NOT NULL, duration INTEGER NOT NULL, name TEXT, flags INTEGER, mode INTEGER DEFAULT (0), ret INTEGER, errno INTEGER DEFAULT (0))"
#define insert_open "INSERT INTO open_table VALUES (@host, @time, @pid, @tid, @duration, @name, @flags, @mode, @ret, @errno)"

#define create_close "CREATE TABLE IF NOT EXISTS close_table ( hostname TEXT NOT NULL, timestamp INTEGER NOT NULL, pid INTEGER NOT NULL, tid INTEGER NOT NULL, duration INTEGER NOT NULL, fd INTEGER NOT NULL, ret INTEGER, errno INTEGER DEFAULT (0))"
#define insert_close "INSERT INTO close_table VALUES (@host, @time, @pid, @tid, @duration, @fd, @ret, @errno)"

#define create_read "CREATE TABLE IF NOT EXISTS read_table (hostname TEXT, timestamp INTEGER, pid INTEGER, tid INTEGER, duration INTEGER, fd INTEGER, count INTEGER, ret INTEGER, errno INTEGER)"
#define insert_read "INSERT INTO read_table VALUES (@host, @time, @pid, @tid, @duration, @fd, @count, @ret, @errno)"

#define create_write "CREATE TABLE IF NOT EXISTS write_table (hostname TEXT, timestamp INTEGER, pid INTEGER, tid INTEGER, duration INTEGER, fd INTEGER, count INTEGER, ret INTEGER, errno INTEGER)"
#define insert_write "INSERT INTO write_table VALUES (@host, @time, @pid, @tid, @duration, @fd, @count, @ret, @errno)"

void* transaction_thread(void *sqldb)
{
	sqlite3 *db = (sqlite3 *) sqldb;
	while(1) {
		sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
		sleep(2);
		sqlite3_exec(db, "END TRANSACTION", NULL, NULL, NULL);		
	};
	pthread_exit(NULL);
}


sqlite_adapter_t* open_database(const char *db_file_name)
{
	sqlite_adapter_t *db = malloc(sizeof(sqlite_adapter_t));	

	sqlite3_open(db_file_name, &((*db).db));
	
	//temp test
	sqlite3_exec((*db).db, "DROP TABLE open_table", NULL, NULL, NULL);
	sqlite3_exec((*db).db, "DROP TABLE close_table", NULL, NULL, NULL);
	sqlite3_exec((*db).db, "DROP TABLE read_table", NULL, NULL, NULL);
	sqlite3_exec((*db).db, "DROP TABLE write_table", NULL, NULL, NULL);
	

	sqlite3_exec((*db).db, create_open, NULL, NULL, NULL);
	sqlite3_exec((*db).db, create_close, NULL, NULL, NULL);
	sqlite3_exec((*db).db, create_read, NULL, NULL, NULL);
	sqlite3_exec((*db).db, create_write, NULL, NULL, NULL);

	sqlite3_exec((*db).db, "PRAGMA synchronous = OFF", NULL, NULL, NULL);
	sqlite3_exec((*db).db, "PRAGMA journal_mode = OFF", NULL, NULL, NULL);
	
	strncpy((*db).db_name, db_file_name, 1023); (*db).db_name[1023]=0;

	//prepare statements
	sqlite3_prepare_v2((*db).db, insert_open, strlen(insert_open)+1, &((*db).open_insert_stmt), NULL); sqlite3_clear_bindings((*db).open_insert_stmt); sqlite3_reset((*db).open_insert_stmt);
	sqlite3_prepare_v2((*db).db, insert_close, strlen(insert_close)+1, &((*db).close_insert_stmt), NULL); sqlite3_clear_bindings((*db).close_insert_stmt); sqlite3_reset((*db).close_insert_stmt);
	sqlite3_prepare_v2((*db).db, insert_read, strlen(insert_read)+1, &((*db).read_insert_stmt), NULL); sqlite3_clear_bindings((*db).read_insert_stmt); sqlite3_reset((*db).read_insert_stmt);
	sqlite3_prepare_v2((*db).db, insert_write, strlen(insert_write)+1, &((*db).write_insert_stmt), NULL); sqlite3_clear_bindings((*db).write_insert_stmt); sqlite3_reset((*db).write_insert_stmt);

	pthread_create(&(*db).tthread, NULL, transaction_thread, (void*) (*db).db);

	return db;
}


void close_database(sqlite_adapter_t *adapter)
{
	if(!adapter)
		return;
	// clean up!
	sqlite3_close((*adapter).db);
	free (adapter);
}

void insert_data(sqlite_adapter_t *adapter, const char *hostname, void *data)
{
	sqlite3 *db = (*adapter).db;
	sqlite3_stmt *stmt = NULL;
	
	enum op_type *type = (enum op_type *)data;

	switch(*type) {
		case READ:
		{
			opfd_t *operation = data;
			/*fprintf(stderr,"[%d:%d] read: %d, %d : %d | duration: %d\n", 
				(*operation).pid, (*operation).tid,
				(*operation).data.read_data.fd,
				(*operation).data.read_data.count,
				(*operation).data.read_data.ret,
				(*operation).duration);*/
			stmt = (*adapter).read_insert_stmt;
			//(@host, @time, @pid, @tid, @duration, @fd, @count, @ret, @errno)
			sqlite3_bind_text (stmt, 1, hostname, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(stmt, 2, (*operation).timestamp);
			sqlite3_bind_int  (stmt, 3, (*operation).pid);
			sqlite3_bind_int  (stmt, 4, (*operation).tid);
			sqlite3_bind_int64(stmt, 5, (*operation).duration);
			sqlite3_bind_int  (stmt, 6, (*operation).data.read_data.fd);
			sqlite3_bind_int  (stmt, 7, (*operation).data.read_data.count);
			sqlite3_bind_int  (stmt, 8, (*operation).data.read_data.ret);
			sqlite3_bind_int  (stmt, 9, (*operation).err);
			
			sqlite3_step(stmt);
			sqlite3_clear_bindings(stmt);
			sqlite3_reset(stmt);
		}	
			break;
		case WRITE:
		{		
			opfd_t *operation = data;
			/*fprintf(stderr,"[%d:%d] write: %d, %d : %d | duration: %d\n", 
				(*operation).pid, (*operation).tid,
				(*operation).data.write_data.fd,
				(*operation).data.write_data.count,
				(*operation).data.write_data.ret,
				(*operation).duration);*/

			stmt = (*adapter).write_insert_stmt;
			//(@host, @time, @pid, @tid, @duration, @fd, @count, @ret, @errno)
			sqlite3_bind_text (stmt, 1, hostname, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(stmt, 2, (*operation).timestamp);
			sqlite3_bind_int  (stmt, 3, (*operation).pid);
			sqlite3_bind_int  (stmt, 4, (*operation).tid);
			sqlite3_bind_int64(stmt, 5, (*operation).duration);
			sqlite3_bind_int  (stmt, 6, (*operation).data.write_data.fd);
			sqlite3_bind_int  (stmt, 7, (*operation).data.write_data.count);
			sqlite3_bind_int  (stmt, 8, (*operation).data.write_data.ret);
			sqlite3_bind_int  (stmt, 9, (*operation).err);
			
			sqlite3_step(stmt);
			sqlite3_clear_bindings(stmt);
			sqlite3_reset(stmt);
		}
			break;
		case OPEN:
		{
			opname_t *operation = data;
			/*fprintf(stderr,"[%d:%d] open: %s, %d : %d | duration: %d\n", 
				(*operation).pid, (*operation).tid,
				(*operation).name,
				(*operation).data.open_data.flags,
				(*operation).data.open_data.ret,
				(*operation).duration);*/

			stmt = (*adapter).open_insert_stmt;
			//@host, @time, @pid, @tid, @duration, @name, @flags, @mode, @ret, @errno
			sqlite3_bind_text (stmt, 1, hostname, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(stmt, 2, (*operation).timestamp);
			sqlite3_bind_int  (stmt, 3, (*operation).pid);
			sqlite3_bind_int  (stmt, 4, (*operation).tid);
			sqlite3_bind_int64(stmt, 5, (*operation).duration);
			sqlite3_bind_text (stmt, 6, (*operation).name, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int  (stmt, 7, (*operation).data.open_data.flags);
			sqlite3_bind_int  (stmt, 8, (*operation).data.open_data.mode);
			sqlite3_bind_int  (stmt, 9, (*operation).data.open_data.ret);
			sqlite3_bind_int  (stmt, 10, (*operation).err);
			
			sqlite3_step(stmt);
			sqlite3_clear_bindings(stmt);
			sqlite3_reset(stmt);
		}
			break;
		case CLOSE:
		{		
			opfd_t *operation = data;
			/*fprintf(stderr,"[%d:%d] close: %d : %d | duration: %d\n", 
				(*operation).pid, (*operation).tid,
				(*operation).data.close_data.fd,
				(*operation).data.close_data.ret,
				(*operation).duration);*/

			stmt = (*adapter).close_insert_stmt;
			//(@host, @time, @pid, @tid, @duration, @fd, @ret, @errno)
			sqlite3_bind_text (stmt, 1, hostname, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(stmt, 2, (*operation).timestamp);
			sqlite3_bind_int  (stmt, 3, (*operation).pid);
			sqlite3_bind_int  (stmt, 4, (*operation).tid);
			sqlite3_bind_int64(stmt, 5, (*operation).duration);
			sqlite3_bind_int  (stmt, 6, (*operation).data.close_data.fd);
			sqlite3_bind_int  (stmt, 7, (*operation).data.close_data.ret);
			sqlite3_bind_int  (stmt, 8, (*operation).err);
			
			sqlite3_step(stmt);
			sqlite3_clear_bindings(stmt);
			sqlite3_reset(stmt);
		}
			break;
		default:
			printf("This shouldn't not happen! Msg type %d\n", (int)*type);
			break;
	};
}
