#ifndef _REDIS_ADAPTER_H
#define _REDIS_ADAPTER_H

#include <hiredis.h>

typedef struct redis_adapter {
	redisContext *db;
	char db_name[1024];

} redis_adapter_t;

redis_adapter_t*  redis_connect(char *hostname, unsigned int port);
void              redis_disconnect(redis_adapter_t *adapter);
void              redis_insert_data(redis_adapter_t *adapter, const char *hostname, void *operation);

ssize_t           redis_get_rhist_json(redis_adapter_t *adapter, char **data);
ssize_t           redis_get_whist_json(redis_adapter_t *adapter, char **data);

#endif // _REDIS_ADAPTER_H
