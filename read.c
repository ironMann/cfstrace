#define _GNU_SOURCE
#include <dlfcn.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include <sys/types.h>
#include <unistd.h>
#include <syscall.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <pthread.h>
#include <semaphore.h>

#include <zmq.h>

#include "libread.h"
#include "shm_cbuffer.h"


unsigned long long cycleCount() {
  asm ("rdtsc");
}

////////////////////////////////////////////////////////////////////////////////
int init = 0, in_init = 0;
shm_cbuffer_t *data_storage;

void teardown_profile_lib()
{
	//terminate = 1;
	//printf("Bye, bye!\n");
	fflush(NULL);
	//sleep(2);
	//zmq_term(msg_context);
	shm_cbuffer_close(data_storage);
}
inline uint64_t timestamp()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_usec + 1000000*tv.tv_sec;
}

inline void fill_header(ops_t *op, enum op_type op_type)
{	
	(*op).operation = op_type;
	(*op).pid = getpid();
	(*op).tid = syscall(SYS_gettid); //gettid();
	(*op).timestamp = timestamp();
}


volatile int init_profile_lib()
{
	int i;
	
	if(init)
		return init;
	in_init = 1;

	if(i = shm_cbuffer_open(&data_storage, "/fsprof") < 0) {
	
		printf("shm failed: %d\n", shm_cbuffer_open(&data_storage, "/fsprof"));
		return init;
	}

	assert (i%16 == 0);

	atexit(teardown_profile_lib);
	init = 1; in_init = 0;
	return init;
}


ssize_t read(int fd, void *buf, size_t count)
{
	static ssize_t(*do_real_read)(int,void*,size_t) = NULL;
	if(!do_real_read) {
		do_real_read = dlsym(RTLD_NEXT, "read");
	}
	
	struct ops operation;
	fill_header(&operation, READ);
	
	ssize_t ret = do_real_read(fd, buf, count);
	if(fd < 3) // do not log stdio
		return ret;

	operation.duration = timestamp() - operation.timestamp;	

	if(!in_init && init_profile_lib()) {
		operation.data.read_data.fd = fd;
		operation.data.read_data.count = count;
		operation.data.read_data.ret = ret;
		operation.err = errno;

		shm_cbuffer_tryput(data_storage, &operation, sizeof(struct ops));
	}
	return ret;
}

ssize_t write(int fd, const void *buf, size_t count)
{
	static ssize_t(*do_real_write)(int,const void*,size_t) = NULL;
	if(!do_real_write) {
		do_real_write = dlsym(RTLD_NEXT, "write");
	}
	
	struct ops operation;
	fill_header(&operation, WRITE);
	
	ssize_t ret = do_real_write(fd, buf, count);
	if(fd < 3) // do not log stdio
		return ret;
	
	operation.duration = timestamp() - operation.timestamp;
	
	if(!in_init && init_profile_lib()) {
		operation.data.write_data.fd = fd;
		operation.data.write_data.count = count;
		operation.data.write_data.ret = ret;
		operation.err = errno;
		
		shm_cbuffer_tryput(data_storage, &operation, sizeof(struct ops));
	}
	return ret;
}


int open64(const char *pathname, int flags, ...)
{
	static int(*do_real_open)(const char *, int, ...) = NULL;
	if(!do_real_open) {
		do_real_open = dlsym(RTLD_NEXT, "open");
	}
	int mode = 0;

	struct ops operation;
	fill_header(&operation, OPEN);
	if(flags & O_CREAT)
	{
		va_list arg;
		va_start(arg, flags);
		mode = va_arg(arg, int);
		va_end(arg);
	}

	int ret = do_real_open(pathname, flags, mode);
	
	operation.duration = timestamp() - operation.timestamp;
	
	if(!in_init && init_profile_lib()) {
		operation.data.open_data.flags = flags;
		strncpy(operation.data.open_data.name, pathname, MAX_PATH_SIZE_TRACE-1); //abs path!
		operation.data.open_data.ret = ret;
		operation.err = errno;
		
		shm_cbuffer_tryput(data_storage, &operation, sizeof(struct ops));
	}
	return ret;
}

int open(const char *pathname, int flags, ...)
{
	int mode = 0;
	if(flags & O_CREAT)
	{
		va_list arg;
		va_start(arg, flags);
		mode = va_arg(arg, int);
		va_end(arg);
	}
	return open64(pathname, flags, mode);
}

int close(int fd)
{
	static int(*do_real_close)(int) = NULL;
	if(!do_real_close) {
		do_real_close = dlsym(RTLD_NEXT, "close");
	}

	struct ops operation;
	fill_header(&operation, CLOSE);
	
	int ret = do_real_close(fd);
	if(fd<3) // strange !!!
		return ret;

	operation.duration = timestamp() - operation.timestamp;
	
	if((in_init==0) && init_profile_lib()) {
		operation.data.close_data.fd = fd;
		operation.data.close_data.ret = ret;
		operation.err = errno;
		
		shm_cbuffer_tryput(data_storage, &operation, sizeof(struct ops));
	}
	return ret;
}




// gcc -fPIC -shared -Wl,-soname,libread.so -ldl -o libread.so  read.c

