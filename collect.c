#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <unistd.h>

#include <pthread.h>
#include <semaphore.h>

#include <zmq.h>

#include "libread.h"
#include "shm_cbuffer.h"


int main( )
{
	void *context = zmq_init(4);


	void *sender_s = zmq_socket(context, ZMQ_PUSH);
	zmq_connect(sender_s, "tcp://localhost:2307");
	
	shm_cbuffer_t *data_storage = NULL;
	if(shm_cbuffer_create(&data_storage, "/fsprof", sizeof(struct ops), 2048) != 0) {
		return -1;
	}

	printf("sizeof struct ops: %d, total shared file ~%d\n", sizeof(ops_t), sizeof(ops_t)*2048); 

	while(1) {
		zmq_msg_t msg;
		zmq_msg_init_size (&msg, sizeof(struct ops));
		shm_cbuffer_get(data_storage, zmq_msg_data(&msg), sizeof(struct ops));
		
		zmq_send(sender_s, &msg, ZMQ_NOBLOCK);

		zmq_msg_close(&msg);
	}
	return 0;
}


