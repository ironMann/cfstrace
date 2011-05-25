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
	char   name[MAX_PATH_SIZE_TRACE];
	int    flags;
	mode_t mode;
	int    ret;
};

struct creat_op {
	char   name[MAX_PATH_SIZE_TRACE];
	mode_t mode;
};

struct close_op {
	int fd;
	int ret;
};

typedef struct opfd {
	enum op_type operation;

	uint64_t timestamp;
	//char     hostname[128];
	pid_t    pid;
	pid_t    tid;

	union {
		struct read_op  read_data;
		struct write_op write_data;
		struct close_op close_data;
	} data;
	
	uint64_t duration;
	int err;

}  __attribute__ ((aligned (16))) opfd_t;


typedef struct opname{
	enum op_type operation;

	uint64_t timestamp;
	//char     hostname[128];
	pid_t    pid;
	pid_t    tid;

	union {
		struct open_op  open_data;
		struct creat_op creat_data;
	} data;
	
	uint64_t duration;
	int err;

}  __attribute__ ((aligned (16))) opname_t;


#endif // _LIBREAD_H


