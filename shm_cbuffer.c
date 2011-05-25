#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h> /* For O_* constants */
#include <unistd.h>

#include <pthread.h>
#include <semaphore.h>

#include "shm_cbuffer.h"

#define init_lock(l) do { pthread_spin_init(l, PTHREAD_PROCESS_SHARED); } while(0)
#define lock(l)      do { pthread_spin_lock(l); } while(0)
#define unlock(l)    do { pthread_spin_unlock(l); } while(0)

#define ALIGN_ON 16


inline static _align(const size_t size)
{
	return ((size+ALIGN_ON-1)/ALIGN_ON)*ALIGN_ON;
}

inline static _align_on(const size_t size, const size_t align)
{
	return ((size+align-1)/align)*align;
}

int shm_cbuffer_create(shm_cbuffer_t **shm_cbuf, const char *name, const size_t obj_size, const size_t count)
{
	assert( 0 < obj_size );
	assert( 0 < count );

	if(obj_size <= 0 || count <= 0)
		return -1;

	// create shared memory region
	// 16-byte alignment for objects
	// total size multiple of page sizes
	long ps = sysconf(_SC_PAGESIZE);
	size_t total_aligned_size = _align(sizeof(shm_cbuffer_t)) + _align(obj_size) * count;
	total_aligned_size = _align_on(total_aligned_size, ps);

	//printf("total_aligned_size :%d\n", total_aligned_size);

	int mem = shm_open(name, O_CREAT | /*O_EXCL*/O_TRUNC | O_RDWR, S_IRUSR|S_IWUSR);
	if(mem == -1)
		return -1;

	ftruncate(mem, total_aligned_size);
	// map shm to addr space
	*shm_cbuf = (shm_cbuffer_t*) mmap(0, total_aligned_size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_LOCKED|MAP_POPULATE, mem, 0);
	if(*shm_cbuf == MAP_FAILED) { //failed because MAP_LOCKED? try without
		*shm_cbuf = (shm_cbuffer_t*) mmap(0, total_aligned_size, PROT_READ|PROT_WRITE, MAP_SHARED | MAP_POPULATE, mem, 0);
		if(*shm_cbuf == MAP_FAILED) { //failed again....
			shm_unlink(name);
			return -1;
		}
	}

	// init header
	memset(*shm_cbuf, 0, sizeof(shm_cbuffer_t));
	(**shm_cbuf).mem = (void*)_align(sizeof(shm_cbuffer_t));
	(**shm_cbuf).size = _align(obj_size);
	(**shm_cbuf).count = count;
	init_lock (&(**shm_cbuf).lock);
	sem_init(&(**shm_cbuf).free, 1, count);
	sem_init(&(**shm_cbuf).used, 1, 0);
	(**shm_cbuf).fd = mem;
	(**shm_cbuf).fsize = total_aligned_size;
	strncpy((**shm_cbuf).name, name, CBUFFER_MAX_NAME-1);

	//printf("mem %d, size %d, count %d, name %s\n", _align(sizeof(shm_cbuffer_t)), (**shm_cbuf).size, (**shm_cbuf).count, (**shm_cbuf).name);

	return 0;
}

// return size of object slot (can be more than obj size created with), -1 on fail
int shm_cbuffer_open(shm_cbuffer_t **shm_cbuf, const char *name)
{
	assert (strlen(name) > 1);
	
	// open shm
	int mem = shm_open(name, O_RDWR, S_IRUSR|S_IWUSR);
	if(mem == -1)
		return -1;

	// get file size...
	struct stat file_stat;
   	if (fstat(mem, &file_stat) == -1) {
		shm_unlink(name);
		return -2;
	}	
    	size_t total_size = file_stat.st_size;
	
	// map shm to addr space
	*shm_cbuf = (shm_cbuffer_t*) mmap(0, total_size, PROT_READ|PROT_WRITE, MAP_SHARED, mem, 0);
	if(*shm_cbuf == MAP_FAILED) {
		shm_unlink(name);
		return -3;
	}

	// check buffer name just in case...
	if(strncmp(name, (**shm_cbuf).name, CBUFFER_MAX_NAME-1) != 0) {
		munmap(*shm_cbuf, total_size);
		shm_unlink(name);
		return -4;
	}
	
	return (int)(**shm_cbuf).size;
}

void shm_cbuffer_destroy(shm_cbuffer_t *shm_cbuf)
{
	char name[CBUFFER_MAX_NAME];
	int fd = (*shm_cbuf).fd;
	size_t fsize = (*shm_cbuf).fsize;
	
	memcpy(name, (*shm_cbuf).name, CBUFFER_MAX_NAME);
	
	munmap(shm_cbuf, fsize);
	shm_unlink(name);
}

void shm_cbuffer_close(shm_cbuffer_t *shm_cbuf)
{
	char name[CBUFFER_MAX_NAME];
	int fd = (*shm_cbuf).fd;
	size_t fsize = (*shm_cbuf).fsize;
	
	memcpy(name, (*shm_cbuf).name, CBUFFER_MAX_NAME);
	
	munmap(shm_cbuf, fsize);
}

void shm_cbuffer_put(shm_cbuffer_t *shm_cbuf, const void *data, size_t size)
{
	int pos;	
	sem_wait(&(*shm_cbuf).free);
	lock(&(*shm_cbuf).lock);
	pos = (*shm_cbuf).h;
	(*shm_cbuf).h = ((*shm_cbuf).h+1) % (*shm_cbuf).count;

	memcpy(((void*)shm_cbuf)+(size_t)(*shm_cbuf).mem+pos*(*shm_cbuf).size, data, size<(*shm_cbuf).size?size:(*shm_cbuf).size);
	
	unlock(&(*shm_cbuf).lock);
	sem_post(&(*shm_cbuf).used);
}

int shm_cbuffer_tryput(shm_cbuffer_t *shm_cbuf, const void *data, size_t size)
{
	int pos;	
	if(sem_trywait(&(*shm_cbuf).free) != 0)
		return -1;
	lock(&(*shm_cbuf).lock);
	pos = (*shm_cbuf).h;
	(*shm_cbuf).h = ((*shm_cbuf).h+1) % (*shm_cbuf).count;
	
	memcpy(((void*)shm_cbuf)+(size_t)(*shm_cbuf).mem+pos*(*shm_cbuf).size, data, size<(*shm_cbuf).size?size:(*shm_cbuf).size);

	unlock(&(*shm_cbuf).lock);
	sem_post(&(*shm_cbuf).used);
	return 0;
}

void shm_cbuffer_get(shm_cbuffer_t *shm_cbuf, void *data, size_t size)
{
	int pos;	
	sem_wait(&(*shm_cbuf).used);
	
	lock(&(*shm_cbuf).lock);
	pos = (*shm_cbuf).t;
	(*shm_cbuf).t = ((*shm_cbuf).t+1) % (*shm_cbuf).count;
	
	memcpy(data, ((void*)shm_cbuf) + (size_t)(*shm_cbuf).mem+pos*(*shm_cbuf).size, size<(*shm_cbuf).size?size:(*shm_cbuf).size);
	
	unlock(&(*shm_cbuf).lock);
	sem_post(&(*shm_cbuf).free);
}




#if 0
int main()
{
	shm_cbuffer_t *buf = NULL;
	shm_cbuffer_create(&buf, "/test", 1, 1);

	printf("sizeof shm_cbuffer_t : %d\n", sizeof(shm_cbuffer_t));
	printf("object size : %d\n", shm_cbuffer_open(&buf, "/test"));
	
	shm_cbuffer_destroy(buf);
	
	return 0;
}
#endif


