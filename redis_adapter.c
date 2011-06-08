#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>

#include "cfstrace.h"
#include "redis_adapter.h"

#include <hiredis.h>

static const char* U_R_HIST_512 = "ZINCRBY r_hist_512 1 %d";
static const char* U_W_HIST_512 = "ZINCRBY w_hist_512 1 %d";

static const char* G_R_HIST_512 = "ZRANGEBYSCORE r_hist_512 -inf +inf WITHSCORES";
static const char* G_W_HIST_512 = "ZRANGEBYSCORE w_hist_512 -inf +inf WITHSCORES";

redis_adapter_t* redis_connect(char *hostname, unsigned int port)
{
	
	assert(hostname != NULL);
	//assert(port > 0);

	redis_adapter_t *db = malloc(sizeof(redis_adapter_t));
	if(!db)
		return NULL;	

	if(strlen(hostname) == 0) {
		hostname = "127.0.0.1";
		port = 6379;
	}

	(*db).db = redisConnect(hostname, port);
	redisContext *c = (*db).db;
	if(c->err) {
		printf("Redis adapter error: %s\n", c->errstr);
		free(db);
		return NULL;
	}
	return db;
}


void redis_disconnect(redis_adapter_t *adapter)
{
	if(!adapter)
		return;
	// clean up!
	redisFree((*adapter).db);
	free (adapter);
}

void redis_insert_data(redis_adapter_t *adapter, const char *hostname, void *data)
{
	redisContext *c = (*adapter).db;
	redisReply *reply = NULL;

	const enum op_type *type = (enum op_type *)data;
	switch(*type) {
		case READ:
		{
			opfd_t *operation = data;

			reply = redisCommand(c, U_R_HIST_512, ((*operation).data.read_data.ret+511)/512*512);
			freeReplyObject(reply);
		}
			break;
		case WRITE:
		{
			opfd_t *operation = data;

			reply = redisCommand(c, U_W_HIST_512, ((*operation).data.write_data.ret+511)/512*512);
			freeReplyObject(reply);
		}
			break;
		case OPEN:
		{
#if 0
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
			sqlite3_bind_int64(stmt, 2, (*operation).header.timestamp);
			sqlite3_bind_int  (stmt, 3, (*operation).header.pid);
			sqlite3_bind_int  (stmt, 4, (*operation).header.tid);
			sqlite3_bind_int64(stmt, 5, (*operation).header.duration);
			sqlite3_bind_text (stmt, 6, (*operation).name, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int  (stmt, 7, (*operation).data.open_data.flags);
			sqlite3_bind_int  (stmt, 8, (*operation).data.open_data.mode);
			sqlite3_bind_int  (stmt, 9, (*operation).data.open_data.ret);
			sqlite3_bind_int  (stmt, 10, (*operation).header.err);

			sqlite3_step(stmt);
			sqlite3_clear_bindings(stmt);
			sqlite3_reset(stmt);
#endif
		}
			break;
		case CLOSE:
		{
#if 0
			opfd_t *operation = data;

			stmt = (*adapter).close_insert_stmt;
			//(@host, @time, @pid, @tid, @duration, @fd, @ret, @errno)
			sqlite3_bind_text (stmt, 1, hostname, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(stmt, 2, (*operation).header.timestamp);
			sqlite3_bind_int  (stmt, 3, (*operation).header.pid);
			sqlite3_bind_int  (stmt, 4, (*operation).header.tid);
			sqlite3_bind_int64(stmt, 5, (*operation).header.duration);
			sqlite3_bind_int  (stmt, 6, (*operation).data.close_data.fd);
			sqlite3_bind_int  (stmt, 7, (*operation).data.close_data.ret);
			sqlite3_bind_int  (stmt, 8, (*operation).header.err);

			sqlite3_step(stmt);
			sqlite3_clear_bindings(stmt);
			sqlite3_reset(stmt);
#endif
		}
			break;
		case PROC_START:
		{
#if 0
			opname_t *operation = data;

			stmt = (*adapter).proc_start_insert_stmt;
			//(@host, @time, @pid, @name)
			sqlite3_bind_text (stmt, 1, hostname, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(stmt, 2, (*operation).header.timestamp);
			sqlite3_bind_int  (stmt, 3, (*operation).header.pid);
			sqlite3_bind_text (stmt, 4, (*operation).name, -1, SQLITE_TRANSIENT);

			sqlite3_step(stmt);
			sqlite3_clear_bindings(stmt);
			sqlite3_reset(stmt);
#endif
		}
			break;
		case PROC_CLOSE:
		{
#if 0
			opfd_t *operation = data;

			stmt = (*adapter).proc_end_insert_stmt;
			//(@host, @time, @pid, @name)
			sqlite3_bind_text (stmt, 1, hostname, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(stmt, 2, (*operation).header.timestamp);
			sqlite3_bind_int  (stmt, 3, (*operation).header.pid);

			sqlite3_step(stmt);
			sqlite3_clear_bindings(stmt);
			sqlite3_reset(stmt);
#endif
		}
			break;
		default:
			printf("This shouldn't not happen! Msg type %d\n", (int)*type);
			break;
	};

	//
}


ssize_t redis_get_rhist_json(redis_adapter_t *adapter, char **data)
{
	redisContext *c = (*adapter).db;
	redisReply *reply = NULL;
	
	*data = malloc(10*1024);
	strcpy(*data, "({ \"data\": [ ");
	
	reply = redisCommand(c, G_R_HIST_512);
	int i;
	for(i=0; i<reply->elements; i+=2) {
		char point[64];
		if(atol(reply->element[i]->str) > 50000) continue;
		sprintf(point, "[%s, %s]", reply->element[i]->str, reply->element[i+1]->str);

		strcat(*data, point);
		if((i+2) < reply->elements)
			strcat(*data, ", ");
	}
	
	strcat(*data, " ] })");

	freeReplyObject(reply);
	
	return strlen(*data);
}

ssize_t           redis_get_whist_json(redis_adapter_t *adapter, char **data);

