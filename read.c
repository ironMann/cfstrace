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
#include "shm_mbuffer.h"


unsigned long long cycleCount() {
  asm ("rdtsc");
}

////////////////////////////////////////////////////////////////////////////////
int init = 0, in_init = 0;
shm_mbuffer_t *fd_data_storage;
shm_mbuffer_t *name_data_storage;

void teardown_profile_lib()
{
	//terminate = 1;
	//printf("Bye, bye!\n");
	fflush(NULL);
	//sleep(2);
	//zmq_term(msg_context);
	shm_mbuffer_close(fd_data_storage);
	shm_mbuffer_close(name_data_storage);
}
inline uint64_t timestamp()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_usec + 1000000*tv.tv_sec;
}

inline void fill_name_header(opname_t *op, enum op_type op_type)
{	
	(*op).operation = op_type;
	(*op).pid = getpid();
	(*op).tid = syscall(SYS_gettid); //gettid();
	(*op).timestamp = timestamp();
}

inline void fill_fd_header(opfd_t *op, enum op_type op_type)
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

	if(i = shm_mbuffer_open(&fd_data_storage, "/cfsprof_fd") < 0) {
		printf("shm failed: %d\n", i);
		return init;
	}

	if(i = shm_mbuffer_open(&name_data_storage, "/cfsprof_name") < 0) {
		printf("shm failed: %d\n", i);
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

	if(fd < 3) // do not log stdio
		return do_real_read(fd, buf, count);
	
	mbuffer_key_t wkey;
	opfd_t *operation;
	if(!in_init && init_profile_lib()) {
		operation = (opfd_t *) shm_mbuffer_tryget_write(fd_data_storage, &wkey);
		if(operation == NULL) //full buffer!!!
			return do_real_read(fd, buf, count);
	}else
		return do_real_read(fd, buf, count);
	
	fill_fd_header(operation, READ);

	ssize_t ret = do_real_read(fd, buf, count);
	
	(*operation).duration = timestamp() - (*operation).timestamp;	
	(*operation).data.read_data.fd = fd;
	(*operation).data.read_data.count = count;
	(*operation).data.read_data.ret = ret;
	(*operation).err = errno;

	shm_mbuffer_put_write(fd_data_storage, wkey);

	return ret;
}

ssize_t write(int fd, const void *buf, size_t count)
{
	static ssize_t(*do_real_write)(int,const void*,size_t) = NULL;
	if(!do_real_write) {
		do_real_write = dlsym(RTLD_NEXT, "write");
	}
	
	if(fd < 3) // do not log stdio
		return do_real_write(fd, buf, count);

	mbuffer_key_t wkey;
	opfd_t *operation;
	if(!in_init && init_profile_lib()) {
		operation = (opfd_t *)shm_mbuffer_tryget_write(fd_data_storage, &wkey);
		if(operation == NULL) //full buffer!!!
			return do_real_write(fd, buf, count);
	} else
		return do_real_write(fd, buf, count);

	fill_fd_header(operation, WRITE);

	ssize_t ret = do_real_write(fd, buf, count);

	(*operation).duration = timestamp() - (*operation).timestamp;


	(*operation).data.write_data.fd = fd;
	(*operation).data.write_data.count = count;
	(*operation).data.write_data.ret = ret;
	(*operation).err = errno;
	
	shm_mbuffer_put_write(fd_data_storage, wkey);

	return ret;
}


int open64(const char *pathname, int flags, ...)
{
	static int(*do_real_open)(const char *, int, ...) = NULL;
	if(!do_real_open) {
		do_real_open = dlsym(RTLD_NEXT, "open");
	}
	int mode = 0;

	if(flags & O_CREAT)
	{
		va_list arg;
		va_start(arg, flags);
		mode = va_arg(arg, int);
		va_end(arg);
	}

	mbuffer_key_t wkey;
	opname_t *operation;
	if(!in_init && init_profile_lib()) {
		operation = (opname_t *)shm_mbuffer_tryget_write(name_data_storage, &wkey);
		if(operation == NULL) //full buffer!!!
			return do_real_open(pathname, flags, mode);
	} else
		return do_real_open(pathname, flags, mode);

	fill_name_header(operation, OPEN);
	
	int ret = do_real_open(pathname, flags, mode);
	
	(*operation).duration = timestamp() - (*operation).timestamp;
	
	(*operation).data.open_data.flags = flags;
	strncpy((*operation).data.open_data.name, pathname, MAX_PATH_SIZE_TRACE-1); //abs path!
	(*operation).data.open_data.ret = ret;
	(*operation).err = errno;
	
	shm_mbuffer_put_write(name_data_storage, wkey);

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

	if(fd<3) // strange !?
		return do_real_close(fd);
	
	mbuffer_key_t wkey;
	opfd_t *operation;
	if(!in_init && init_profile_lib()) {
		operation = (opfd_t *)shm_mbuffer_tryget_write(fd_data_storage, &wkey);
		if(operation == NULL) //full buffer!
			return do_real_close(fd);
	} else
		return do_real_close(fd);

	fill_fd_header(operation, CLOSE);
	
	int ret = do_real_close(fd);
	
	(*operation).duration = timestamp() - (*operation).timestamp;
	
	
	(*operation).data.close_data.fd = fd;
	(*operation).data.close_data.ret = ret;
	(*operation).err = errno;
	
	shm_mbuffer_put_write(fd_data_storage, wkey);
	
	return ret;
}


// gcc -fPIC -shared -Wl,-soname,libread.so -ldl -o libread.so  read.c

