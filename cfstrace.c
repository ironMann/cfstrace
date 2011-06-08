#define _GNU_SOURCE
#include <dlfcn.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>

#include <sys/types.h>
#include <unistd.h>
#include <syscall.h>
#include <sys/stat.h>
#include <fcntl.h>


#include <pthread.h>
#include <semaphore.h>

#include "cfstrace.h"
#include "shm_mbuffer.h"

#include <signal.h>

void (*signal(int signum, void (*sighandler)(int)))(int);

static int read_cmdline(char *dst, unsigned int size);

unsigned long long cycleCount() {
  asm ("rdtsc");
}

void handler(int sig) {
  void *array[10];
  size_t size;

  // get void*'s for all entries on the stack
  size = backtrace(array, 10);

  // print out all the frames to stderr
  fprintf(stderr, "Error: signal %d:\n", sig);
  backtrace_symbols_fd(array, size, 2);
  exit(1);
}


static inline uint64_t timestamp()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint64_t)tv.tv_usec + (uint64_t)1000000 * (uint64_t)tv.tv_sec;
}

static inline void fill_header(op_header_t *op, enum op_type op_type)
{
	(*op).operation = op_type;
	(*op).pid = getpid();
	(*op).tid = syscall(SYS_gettid); //gettid();
	(*op).timestamp = timestamp();
}


////////////////////////////////////////////////////////////////////////////////

static int init = 0, in_init = 0;
static shm_mbuffer_t *fd_data_storage = NULL;
static shm_mbuffer_t *name_data_storage = NULL;
static int gpid =0;

void teardown_profile_lib()
{
	//if not used, return
	if(!init) return;
	if(in_init) return;
	if(!fd_data_storage) return;
	if(!name_data_storage) return;
	if(!gpid) return;	
	if(gpid != getpid()) return;

	// put proc end message
	mbuffer_key_t wkey;
	opfd_t *operation = (opfd_t *)shm_mbuffer_get_write(fd_data_storage, &wkey);
	fill_header(&(*operation).header, PROC_CLOSE);
	shm_mbuffer_put_write(fd_data_storage, wkey);

	shm_mbuffer_close(fd_data_storage);
	shm_mbuffer_close(name_data_storage);
}

int read_abspath(int fd, char *dst, unsigned int size)
{
	char name[32];
	ssize_t ret;
	dst[0] = '\0';
	snprintf(name, sizeof name, "/proc/self/fd/%d", fd);

	if((ret=readlink(name, dst, size)) <= 0) {
		dst[0] = '\0';
		return 0;
	}

	if((ret>0) && (dst[0]!='/')) {
		dst[0] = '\0';
		return 0;
	}

	dst[ret] = '\0';
	return ret;
}



int init_profile_lib()
{
	int i;
	if(gpid == 0) {
		gpid = getpid(); // first process
		goto reinit;
	}

	if(gpid != getpid()) {// fork/vfork! reinit
		goto reinit;
	}

	if(init) // ok!
		return init;

reinit:
	in_init = 1;

	if(i = shm_mbuffer_open(&fd_data_storage, "/cfsprof_fd") < 0) {
		printf("shm %s failed: %d\n", "/cfsprof_fd", i);
		return init;
	}

	if(i = shm_mbuffer_open(&name_data_storage, "/cfsprof_name") < 0) {
		printf("shm %s failed: %d\n", "/cfsprof_name", i);
		return init;
	}

	//insert procstart message
	mbuffer_key_t wkey;
	opname_t *operation = (opname_t *)shm_mbuffer_get_write(name_data_storage, &wkey);

	fill_header(&(*operation).header, PROC_START);
	read_cmdline((*operation).name, MAX_PATH_SIZE_TRACE);
	(*operation).name[MAX_PATH_SIZE_TRACE-1] = 0;

	shm_mbuffer_put_write(name_data_storage, wkey);

	atexit(teardown_profile_lib);
	//gpid = getpid();
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

	op_header_t header;

	fill_header(&header, READ);

	ssize_t ret = do_real_read(fd, buf, count);

	mbuffer_key_t wkey;
	opfd_t *operation;
	if(!in_init && init_profile_lib()) {
		operation = (opfd_t *) shm_mbuffer_get_write(fd_data_storage, &wkey);
		if(operation == NULL) //full buffer!!!
			return ret;
	}else
		return ret;

	memcpy(operation, &header, sizeof(op_header_t));

	(*operation).header.duration = timestamp() - (*operation).header.timestamp;
	(*operation).data.read_data.fd = fd;
	(*operation).data.read_data.count = count;
	(*operation).data.read_data.ret = ret;
	(*operation).header.err = errno;

	shm_mbuffer_put_write(fd_data_storage, wkey);

	return ret;
}

ssize_t write(int fd, const void *buf, size_t count)
{
	static ssize_t(*do_real_write)(int,const void*,size_t) = NULL;
	if(!do_real_write) {
		do_real_write = dlsym(RTLD_NEXT, "write");
	}

	//printf("write : %d\n", fd);

	if(fd < 3) // do not log stdio
		return do_real_write(fd, buf, count);

	op_header_t header;

	fill_header(&header, WRITE);

	ssize_t ret = do_real_write(fd, buf, count);

	mbuffer_key_t wkey;
	opfd_t *operation;
	if(!in_init && init_profile_lib()) {
		operation = (opfd_t *)shm_mbuffer_get_write(fd_data_storage, &wkey);
		if(operation == NULL) //full buffer!!!
			return ret;
	} else
		return ret;

	memcpy(operation, &header, sizeof(op_header_t));
	(*operation).header.duration = timestamp() - (*operation).header.timestamp;
	(*operation).data.write_data.fd = fd;
	(*operation).data.write_data.count = count;
	(*operation).data.write_data.ret = ret;
	(*operation).header.err = errno;

	shm_mbuffer_put_write(fd_data_storage, wkey);

	return ret;
}

ssize_t pread64 (int fd, void *buf, size_t nbytes, __off64_t offset)
{
	static ssize_t (*do_real_pread) (int fd, void *buf, size_t nbytes, __off64_t offset) = NULL;
	if(!do_real_pread) {
		do_real_pread = dlsym(RTLD_NEXT, "pread64");
	}

	if(fd < 3) // do not log stdio
		return do_real_pread(fd, buf, nbytes, offset);

	return do_real_pread(fd, buf, nbytes, offset);

}

#if 0
/* Write N bytes of BUF to FD at the given position OFFSET without
   changing the file pointer.  Return the number written, or -1.  */
extern ssize_t pwrite64 (int __fd, __const void *__buf, size_t __n,
                         __off64_t __offset) __wur;

#endif

int open64(const char *pathname, int flags, ...)
{
	static int(*do_real_open)(const char *, int, ...) = NULL;
	if(!do_real_open) {
		do_real_open = dlsym(RTLD_NEXT, "open64");
	}
	int mode = 0;

	if(flags & O_CREAT)
	{
		va_list arg;
		va_start(arg, flags);
		mode = va_arg(arg, int);
		va_end(arg);
	}

	op_header_t header;
	fill_header(&header, OPEN);

	int ret = do_real_open(pathname, flags, mode);

	mbuffer_key_t wkey;
	opname_t *operation;
	if(!in_init && init_profile_lib()) {
		operation = (opname_t *)shm_mbuffer_get_write(name_data_storage, &wkey);
		if(operation == NULL) //full buffer!!!
			return ret;
	} else
		return ret;

	memcpy(operation, &header, sizeof(op_header_t));
	(*operation).header.duration = timestamp() - (*operation).header.timestamp;

	(*operation).data.open_data.flags = flags;
	(*operation).data.open_data.ret = ret;
	(*operation).header.err = errno;

	if(read_abspath(ret, (*operation).name, MAX_PATH_SIZE_TRACE) <= 0)
		shm_mbuffer_discard_write(name_data_storage, wkey);
	else
		shm_mbuffer_put_write(name_data_storage, wkey);

	return ret;
}

int open(const char *pathname, int flags, ...)
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

	op_header_t header;
	fill_header(&header, OPEN);

	int ret = do_real_open(pathname, flags, mode);

	mbuffer_key_t wkey;
	opname_t *operation;
	if(!in_init && init_profile_lib()) {
		operation = (opname_t *)shm_mbuffer_get_write(name_data_storage, &wkey);
		if(operation == NULL) //full buffer!!!
			return ret;
	} else
		return ret;

	memcpy(operation, &header, sizeof(op_header_t));
	(*operation).header.duration = timestamp() - (*operation).header.timestamp;

	(*operation).data.open_data.flags = flags;
	(*operation).data.open_data.ret = ret;
	(*operation).header.err = errno;

	if(read_abspath(ret, (*operation).name, MAX_PATH_SIZE_TRACE) <= 0)
		shm_mbuffer_discard_write(name_data_storage, wkey);
	else
		shm_mbuffer_put_write(name_data_storage, wkey);

	return ret;
}

int close(int fd)
{
	static int(*do_real_close)(int) = NULL;
	if(!do_real_close) {
		do_real_close = dlsym(RTLD_NEXT, "close");
	}

	if(fd<3) // strange !?
		return do_real_close(fd);

	op_header_t header;
	fill_header(&header, CLOSE);

	int ret = do_real_close(fd);

	mbuffer_key_t wkey;
	opfd_t *operation;
	if(!in_init && init_profile_lib()) {
		operation = (opfd_t *)shm_mbuffer_get_write(fd_data_storage, &wkey);
		if(operation == NULL) //full buffer!
			return ret;
	} else
		return ret;

	memcpy(operation, &header, sizeof(op_header_t));
	(*operation).header.duration = timestamp() - (*operation).header.timestamp;
	(*operation).data.close_data.fd = fd;
	(*operation).data.close_data.ret = ret;
	(*operation).header.err = errno;

	shm_mbuffer_put_write(fd_data_storage, wkey);

	return ret;
}

FILE *fopen(const char *filename, const char *type) {
	static FILE *(*do_real_fopen)(const char *filename, const char *type) = NULL;
	if(!do_real_fopen)
		do_real_fopen = dlsym(RTLD_NEXT, "fopen");

	op_header_t header;
	fill_header(&header, OPEN);

	FILE *ret = do_real_fopen(filename, type);

	mbuffer_key_t wkey;
	opname_t *operation;
	if(!in_init && init_profile_lib()) {
		operation = (opname_t *)shm_mbuffer_get_write(name_data_storage, &wkey);
		if(operation == NULL) //full buffer!!!
			return ret;
	} else
		return ret;

	memcpy(operation, &header, sizeof(op_header_t));
	(*operation).header.duration = timestamp() - (*operation).header.timestamp;

	(*operation).data.open_data.flags = 0;
	(*operation).data.open_data.ret = ret!=NULL?fileno(ret):-1;
	(*operation).header.err = errno;

	if(ret!=NULL)
		read_abspath(fileno(ret), (*operation).name, MAX_PATH_SIZE_TRACE);
	else
		strcpy((*operation).name, filename);

	shm_mbuffer_put_write(name_data_storage, wkey);

	return ret;
}

FILE *fopen64(const char *filename, const char *type)
{
	static FILE *(*do_real_fopen64)(const char *filename, const char *type) = NULL;
	if(!do_real_fopen64)
		do_real_fopen64 = dlsym(RTLD_NEXT, "fopen64");

	//printf("filename %s\n", filename);

	op_header_t header;
	fill_header(&header, OPEN);

	FILE *ret = do_real_fopen64(filename, type);

	mbuffer_key_t wkey;
	opname_t *operation;
	if(!in_init && init_profile_lib()) {
		operation = (opname_t *)shm_mbuffer_get_write(name_data_storage, &wkey);
		if(operation == NULL) //full buffer!!!
			return ret;
	} else
		return ret;

	memcpy(operation, &header, sizeof(op_header_t));
	(*operation).header.duration = timestamp() - (*operation).header.timestamp;

	(*operation).data.open_data.flags = 0;
	(*operation).data.open_data.ret = ret!=NULL?fileno(ret):-1;
	(*operation).header.err = errno;

	if(ret!=NULL)
		read_abspath(fileno(ret), (*operation).name, MAX_PATH_SIZE_TRACE);
	else
		strcpy((*operation).name, filename);

	shm_mbuffer_put_write(name_data_storage, wkey);

	return ret;
}

int fclose(FILE *fp)
{
	static int (*do_real_fclose)(FILE *fp) = NULL;
	if(!do_real_fclose)
		do_real_fclose = dlsym(RTLD_NEXT, "fclose");

	op_header_t header;
	fill_header(&header, CLOSE);

	int ret = do_real_fclose(fp);

	if(fp == stderr || fp ==stdin || fp==stdout || fp==NULL)
		return ret;

	mbuffer_key_t wkey;
	opfd_t *operation;
	if(!in_init && init_profile_lib()) {
		operation = (opfd_t *) shm_mbuffer_get_write(fd_data_storage, &wkey);
		if(operation == NULL) //full buffer!!!
			return ret;
	}else
		return ret;

	memcpy(operation, &header, sizeof(op_header_t));

	(*operation).header.duration = timestamp() - (*operation).header.timestamp;
	(*operation).data.close_data.fd = fp!=NULL?fileno(fp):-1;
	(*operation).data.close_data.ret = ret;
	(*operation).header.err = errno;

	shm_mbuffer_put_write(fd_data_storage, wkey);

	return ret;
}

size_t fread(void * ptr, size_t size, size_t nitems, FILE * stream)
{
	static size_t (*do_real_fread)(void * ptr, size_t size, size_t nitems, FILE * stream) = NULL;
	if(!do_real_fread)
		do_real_fread = dlsym(RTLD_NEXT, "fread");

	op_header_t header;
	fill_header(&header, READ);

	size_t ret = do_real_fread(ptr, size, nitems, stream);

	mbuffer_key_t wkey;
	opfd_t *operation;
	if(!in_init && init_profile_lib()) {
		operation = (opfd_t *) shm_mbuffer_get_write(fd_data_storage, &wkey);
		if(operation == NULL) //full buffer!!!
			return ret;
	}else
		return ret;

	memcpy(operation, &header, sizeof(op_header_t));

	(*operation).header.duration = timestamp() - (*operation).header.timestamp;
	(*operation).data.read_data.fd = stream!=NULL?fileno(stream):-1;
	(*operation).data.read_data.count = size*nitems;
	(*operation).data.read_data.ret = ret;
	(*operation).header.err = errno;

	shm_mbuffer_put_write(fd_data_storage, wkey);

	return ret;
}

size_t fwrite(const void *ptr, size_t size, size_t nitems, FILE *stream)
{
	static size_t (*do_real_fwrite)(const void *ptr, size_t size, size_t nitems, FILE *stream) = NULL;
	if(!do_real_fwrite)
		do_real_fwrite = dlsym(RTLD_NEXT, "fwrite");

	op_header_t header;
	fill_header(&header, WRITE);

	size_t ret = do_real_fwrite(ptr, size, nitems, stream);

	mbuffer_key_t wkey;
	opfd_t *operation;
	if(!in_init && init_profile_lib()) {
		operation = (opfd_t *) shm_mbuffer_get_write(fd_data_storage, &wkey);
		if(operation == NULL) //full buffer!!!
			return ret;
	}else
		return ret;

	memcpy(operation, &header, sizeof(op_header_t));

	(*operation).header.duration = timestamp() - (*operation).header.timestamp;
	(*operation).data.read_data.fd = stream!=NULL?fileno(stream):-1;
	(*operation).data.read_data.count = size*nitems;
	(*operation).data.read_data.ret = ret;
	(*operation).header.err = errno;

	shm_mbuffer_put_write(fd_data_storage, wkey);

	return ret;
}

int fputs(const char *s, FILE *stream)
{
	static int (*do_real_fputs)(const char *s, FILE *stream) = NULL;
	if(!do_real_fputs)
		do_real_fputs = dlsym(RTLD_NEXT, "fputs");

	op_header_t header;
	fill_header(&header, WRITE);

	int ret = do_real_fputs(s, stream);

	mbuffer_key_t wkey;
	opfd_t *operation;
	if(!in_init && init_profile_lib()) {
		operation = (opfd_t *) shm_mbuffer_get_write(fd_data_storage, &wkey);
		if(operation == NULL) //full buffer!!!
			return ret;
	}else
		return ret;

	memcpy(operation, &header, sizeof(op_header_t));

	(*operation).header.duration = timestamp() - (*operation).header.timestamp;
	(*operation).data.read_data.fd = stream!=NULL?fileno(stream):-1;
	(*operation).data.read_data.count = 0;//strlen(s);
	(*operation).data.read_data.ret = ret;
	(*operation).header.err = errno;

	shm_mbuffer_put_write(fd_data_storage, wkey);

	return ret;
}

pid_t vfork(void)
{
	char buffer[4096];
	char *preload = getenv("LD_PRELOAD");
	if(preload)
		strcpy(buffer, preload);

	pid_t pid = fork();
	if(pid==0) {
		//set LD_PRELOAD in child so execve can pick it up (usually comes after vfork)
		setenv("LD_PRELOAD", buffer, 1);
	}

	return pid;
}

int read_cmdline(char *dst, unsigned int size)
{
	static ssize_t(*do_real_read)(int,void*,size_t) = NULL;
	static int(*do_real_open)(const char *, int, ...) = NULL;
	static int(*do_real_close)(int) = NULL;

	if(!do_real_read) {
		do_real_read = dlsym(RTLD_NEXT, "read");
		do_real_open = dlsym(RTLD_NEXT, "open");
		do_real_close = dlsym(RTLD_NEXT, "close");
	}

	char name[32];
	int fd;
	unsigned n = 0;
	dst[0] = '\0';
	snprintf(name, sizeof name, "/proc/self/cmdline");
	fd = do_real_open(name, O_RDONLY);
	if(fd==-1)
		return 0;
	for(;;){
		ssize_t r = do_real_read(fd,dst+n,size-n);
		if(r==-1){
			if(errno==EINTR)
				continue;
			break;
		}
		n += r;
		if(n>=size)
			break; // filled the buffer
		if(r==0)
			break;  // EOF
	}
	do_real_close(fd);
	if(n){
		int i;
		if(n>=size)
			n = size-1;
		dst[n] = '\0';
		i=0;
		while(i<=n){
			int c = dst[i];
			if(c<' ' || c>'~') {
				dst[i]= ' ';
				c = ' ';
			}

			if((c==' ') || (c == '\0')) {
				dst[i]='\0';
				break;
			}
			i++;
		}
		return i;
	}
	return n;
}



