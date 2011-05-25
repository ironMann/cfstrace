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
#include "shm_mbuffer.h"

static void *context = NULL;
static shm_mbuffer_t *fd_data_storage = NULL;
static shm_mbuffer_t *name_data_storage = NULL;

void * fd_worker(void *p)
{
	void *sender_s = zmq_socket(context, ZMQ_PUSH);
	zmq_connect(sender_s, "tcp://localhost:2307");
	
	while(1) {
		zmq_msg_t msg;
		//zmq_msg_init_size (&msg, sizeof(opfd_t));

		mbuffer_key_t read_key;
		opfd_t *operation = (opfd_t*)shm_mbuffer_get_read(fd_data_storage, &read_key);
		

		//memcpy(zmq_msg_data(&msg), operation, sizeof(opfd_t));
		//shm_mbuffer_put_read(fd_data_storage, read_key);
		
		zmq_msg_init_data(&msg, operation, sizeof(opfd_t), shm_mbuff_put_read_zmq, fd_data_storage);

		zmq_send(sender_s, &msg, ZMQ_NOBLOCK);

		zmq_msg_close(&msg);
	}
	pthread_exit(NULL);
}

void * name_worker(void *p)
{
	void *sender_s = zmq_socket(context, ZMQ_PUSH);
	zmq_connect(sender_s, "tcp://localhost:2307");
	
	while(1) {
		zmq_msg_t msg;
		//zmq_msg_init_size (&msg, sizeof(opfd_t));

		mbuffer_key_t read_key;
		opname_t *operation = (opname_t *)shm_mbuffer_get_read(name_data_storage, &read_key);
		

		//memcpy(zmq_msg_data(&msg), operation, sizeof(opfd_t));
		//shm_mbuffer_put_read(fd_data_storage, read_key);
		
		zmq_msg_init_data(&msg, operation, sizeof(opname_t), shm_mbuff_put_read_zmq, name_data_storage);

		zmq_send(sender_s, &msg, ZMQ_NOBLOCK);

		zmq_msg_close(&msg);
	}
	pthread_exit(NULL);
}



int main( )
{
	context = zmq_init(6);

	void *sender_s = zmq_socket(context, ZMQ_PUSH);
	zmq_connect(sender_s, "tcp://localhost:2307");

	if(shm_mbuffer_create(&fd_data_storage, "/cfsprof_fd", sizeof(opfd_t), 2048) != 0) {
		return -1;
	}
	
	if(shm_mbuffer_create(&name_data_storage, "/cfsprof_name", sizeof(opname_t), 2048) != 0) {
		return -1;
	}

	//printf("sizeof struct ops: %d, total shared file ~%d\n", sizeof(ops_t), sizeof(ops_t)*2048); 
	pthread_t tfd;
	pthread_t tname;	

	pthread_create(&tfd, NULL,  fd_worker, (void*) NULL);
	pthread_create(&tname, NULL, name_worker, (void*) NULL);


	while(1) {
		sleep(1);
	}
	return 0;
}


