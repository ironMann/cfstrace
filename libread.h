#ifndef _LIBREAD_H
#define _LIBREAD_H

#include <stdint.h>

#define MAX_PATH_SIZE_TRACE 1024

enum op_type {
		READ,
		WRITE,
		OPEN,
		CREAT,
		CLOSE,
		UNDEFINED };


struct read_op {
	int fd;
	size_t count;
	ssize_t ret;
};

struct write_op {
	int fd;
	size_t count;
	ssize_t ret;
};

struct open_op {
	int    flags;
	mode_t mode;
	int    ret;
};

struct close_op {
	int fd;
	int ret;
};

typedef struct opfd {
	enum op_type operation;

	uint64_t timestamp;
	pid_t    pid;
	pid_t    tid;
		
	uint64_t duration;
	int err;

	union {
		struct read_op  read_data;
		struct write_op write_data;
		struct close_op close_data;
	} data;

}  __attribute__ ((aligned (16))) opfd_t;


typedef struct opname{
	enum op_type operation;

	uint64_t timestamp;
	pid_t    pid;
	pid_t    tid;

	uint64_t duration;
	int err;

	union {
		struct open_op  open_data;
	} data;
	
	char   name[MAX_PATH_SIZE_TRACE];

}  __attribute__ ((aligned (16))) opname_t;


#endif // _LIBREAD_H


