#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfstrace.h"
#include "sqlite_adapter.h"
#include <zmq.h>


int main( )
{
	void *context = zmq_init(4);

	void *receiver_s = zmq_socket(context, ZMQ_PULL);
	zmq_bind(receiver_s, "tcp://*:2307");
	sqlite_adapter_t *db;

	//printf("received packet\n");
	db = open_database("trace.db");
	if(!db)
		return -1;

	while(1) {
		zmq_msg_t msg;
		zmq_msg_init(&msg);
		int ret;
		if((ret = zmq_recv(receiver_s, &msg, 0)) != 0) {
			printf("recieve error: %d %s\nExiting!\n", ret, zmq_strerror(ret));
			return -1;
		}
		//printf("received packet\n");
		
		struct ops * operation = zmq_msg_data(&msg);
		insert_data(db, "vm-machine", operation);
		
		zmq_msg_close(&msg);
	}

	close_database(db);
	return 0;
}
