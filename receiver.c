#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include <sys/socket.h>       /*  socket definitions        */
#include <sys/types.h>        /*  socket types              */
#include <arpa/inet.h>        /*  inet (3) funtions         */

#include "cfstrace.h"
#include "sqlite_adapter.h"
#include "redis_adapter.h"

#include <zmq.h>

#define SERVER_PORT 8765

static void *context = NULL;

void* web_server_thing(void* prdb)
{
	redis_adapter_t *rdb = redis_connect("", 0);

	int    listener, conn;
	pid_t  pid;
	struct sockaddr_in servaddr;

	/*  Create socket  */
	if ( (listener = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
		printf("Couldn't create listening socket.");
		return -1;
	}

	/*  Populate socket address structure  */
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port        = htons(SERVER_PORT);

	/*  Assign socket address to socket  */ 
	if ( bind(listener, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0 ) {
		printf("Couldn't bind listening socket.");
		return -1;
	}

	/*  Make socket a listening socket  */
	if ( listen(listener, 3) < 0 ) {
		printf("Call to listen failed.");
		return -1;
	}

	char rbuffer[4096];

	while(1) {
		printf("\nWainting for web\n");

		conn = accept(listener, NULL, NULL);

		int ret;
		if((ret = recv(conn, rbuffer, 4096, 0)) < 1) {
			printf("recieve error: %d\nExiting!\n", ret);
			return -1;
		}

		printf("Received from above: \n=================================\n%s\n=================================\n", rbuffer);
		//get callback!
		char *p = strstr(rbuffer, "callback=");
		p+= strlen("callback=");
		char callback[1024]; int c =0;
		while(!isspace(*p) && c< 1024 && *p!='&' && *p!='?' && *p!=';')  {
			callback[c] = *p;
			p++;
			c++;
		}
		
		printf("Callback : %s\n", callback);

		char *send_data = NULL;
		int len = redis_get_rhist_json(rdb, &send_data);
		
		char *message = malloc(4096 + len);
		sprintf(message, "HTTP/1.0 200 OK\r\nContent-Type: application/javascript\r\n\r\n%s", callback);
		
		strcat(message, send_data);

		printf("sending : \n%s", message);

		if((ret = send(conn, message, strlen(message), 0)) != strlen(message)) {
			printf("send error: %d\nExiting!\n", ret);
			return -1;
		}

		free(send_data);
		free(message);

		close(conn);
	}
	pthread_exit(NULL);
	return NULL;
}


int main( )
{
	sqlite_adapter_t *db= NULL;
	redis_adapter_t *rdb = NULL;
	pthread_t tweb = NULL;

	context = zmq_init(8);

	void *receiver_s = zmq_socket(context, ZMQ_PULL);
	zmq_bind(receiver_s, "tcp://*:2307");


	db = sqlite_open_database("trace.db");
	rdb = redis_connect("", 0);

	if(!db)
		printf("sqlite adapter failed to initialize!\n");
	if(!rdb)
		printf("redis adapter failed to initialize!\n");

	if(rdb)
		pthread_create(&tweb, NULL, web_server_thing, rdb);

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
		
		sqlite_insert_data(db, "vm-machine", operation);
		redis_insert_data(rdb, "vm-machine", operation);

		zmq_msg_close(&msg);
	}

	sqlite_close_database(db);
	redis_disconnect(rdb);

	return 0;
}



