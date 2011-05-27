#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <unistd.h>

#include <pthread.h>
#include <semaphore.h>

#include <zmq.h>

#include "cfstrace.h"
#include "shm_mbuffer.h"

static void *context = NULL;
static shm_mbuffer_t *fd_data_storage = NULL;
static shm_mbuffer_t *name_data_storage = NULL;

static char cstr[2048];


void * fd_worker(void *p)
{
	void *sender_s = zmq_socket(context, ZMQ_PUSH);

	zmq_connect(sender_s, cstr);

	while(1) {
		zmq_msg_t msg;

		mbuffer_key_t read_key;
		opfd_t *operation = (opfd_t*)shm_mbuffer_get_read(fd_data_storage, &read_key);

		zmq_msg_init_data(&msg, operation, sizeof(opfd_t), shm_mbuff_put_read_zmq, fd_data_storage);

		zmq_send(sender_s, &msg, ZMQ_NOBLOCK);

		zmq_msg_close(&msg);
	}
	pthread_exit(NULL);
}

void * name_worker(void *p)
{
	void *sender_s = zmq_socket(context, ZMQ_PUSH);
	
	zmq_connect(sender_s, cstr);
	
	while(1) {
		zmq_msg_t msg;

		mbuffer_key_t read_key;
		opname_t *operation = (opname_t *)shm_mbuffer_get_read(name_data_storage, &read_key);
		
		size_t real_size = (uintptr_t)(*operation).name - (uintptr_t)operation;
		real_size += strlen((*operation).name) + 1;
		
		zmq_msg_init_data(&msg, operation, real_size, shm_mbuff_put_read_zmq, name_data_storage);

		zmq_send(sender_s, &msg, ZMQ_NOBLOCK);

		zmq_msg_close(&msg);
	}
	pthread_exit(NULL);
}


int main(int c, char **p)
{
	context = zmq_init(8);
	
	if(c == 1)
		sprintf(cstr, "tcp://%s:%d", "localhost", 2307);
	else if(c == 3)
		sprintf(cstr, "tcp://%s:%s", p[1],p[2]);
	else {
		printf("check your inputs\n collect host port\n");
		return 0;
	}

	if(shm_mbuffer_create(&fd_data_storage, "/cfsprof_fd", sizeof(opfd_t), 2048) != 0) {
		return -1;
	}
	
	if(shm_mbuffer_create(&name_data_storage, "/cfsprof_name", sizeof(opname_t), 2048) != 0) {
		return -1;
	}

	pthread_t tfd;
	pthread_t tname;	

	pthread_create(&tfd, NULL,  fd_worker, (void*) NULL);
	pthread_create(&tname, NULL, name_worker, (void*) NULL);

	while(1) {
		sleep(2);
	}
	return 0;
}


