#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h> /* For O_* constants */
#include <unistd.h>

#include <pthread.h>
#include <semaphore.h>

#include "shm_mbuffer.h"

#define init_lock(l) do { pthread_spin_init(l, PTHREAD_PROCESS_SHARED); } while(0)
#define lock(l)      do { pthread_spin_lock(l); } while(0)
#define unlock(l)    do { pthread_spin_unlock(l); } while(0)

#define ALIGN_ON 16

//#define getffsl(x) __builtin_ffsl((x))

inline unsigned int getffsl(uint64_t x)
{
	if(sizeof(uint64_t) == sizeof(unsigned long)) {
		return __builtin_ffsl(x);
	}else {
		union data {
			uint64_t u64;
			unsigned long l32[2];
		} d;
		d.u64 = x;
		int ret;
		if(ret = __builtin_ffsl(d.l32[0]) != 0)
			return ret;
		return __builtin_ffsl(d.l32[1]);
	}
}

inline static size_t _align(const size_t size)
{
	return ((size+ALIGN_ON-1)/ALIGN_ON)*ALIGN_ON;
}

inline static size_t _align_on(const size_t size, const size_t align)
{
	return ((size+align-1)/align)*align;
}

int shm_mbuffer_create(shm_mbuffer_t **shm_mbuf, const char *name, const size_t obj_size, /*const*/ size_t count)
{
	count = 64; //for now
	assert( 0 < obj_size );
	assert( 0 < count );

	if(obj_size <= 0 || count <= 0)
		return -1;

	// create shared memory region
	// 16-byte alignment for objects
	// total size multiple of page sizes
	long ps = sysconf(_SC_PAGESIZE);
	size_t total_aligned_size = _align(sizeof(shm_mbuffer_t)) + _align(obj_size) * count;
	total_aligned_size = _align_on(total_aligned_size, ps);

	int mem = shm_open(name, O_CREAT | /*O_EXCL*/O_TRUNC | O_RDWR, S_IRUSR|S_IWUSR);
	if(mem == -1)
		return -1;

	ftruncate(mem, total_aligned_size);
	// map shm to addr space
	*shm_mbuf = (shm_mbuffer_t*) mmap(0, total_aligned_size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_LOCKED|MAP_POPULATE, mem, 0);
	if(*shm_mbuf == MAP_FAILED) { //failed because MAP_LOCKED? try without
		*shm_mbuf = (shm_mbuffer_t*) mmap(0, total_aligned_size, PROT_READ|PROT_WRITE, MAP_SHARED | MAP_POPULATE, mem, 0);
		if(*shm_mbuf == MAP_FAILED) { //failed again....
			shm_unlink(name);
			return -1;
		}
	}

	// init header
	memset(*shm_mbuf, 0, sizeof(shm_mbuffer_t));
	(**shm_mbuf).mem = (void*)_align(sizeof(shm_mbuffer_t));
	(**shm_mbuf).size = _align(obj_size);
	(**shm_mbuf).count = count;
	init_lock (&(**shm_mbuf).Rlock);
	init_lock (&(**shm_mbuf).Wlock);
	sem_init(&(**shm_mbuf).Wsem, 1, count);
	sem_init(&(**shm_mbuf).Rsem, 1, 0);
	(**shm_mbuf).fd = mem;
	(**shm_mbuf).fsize = total_aligned_size;
	(**shm_mbuf).Rfield = 0x0000000000000000ULL;
	(**shm_mbuf).Wfield = 0xFFFFFFFFFFFFFFFFULL;
	strncpy((**shm_mbuf).name, name, MBUFFER_MAX_NAME-1);

	return 0;
}

// return size of object slot (can be more than obj size created with), -1 on fail
int shm_mbuffer_open(shm_mbuffer_t **shm_mbuf, const char *name)
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
	*shm_mbuf = (shm_mbuffer_t*) mmap(0, total_size, PROT_READ|PROT_WRITE, MAP_SHARED, mem, 0);
	if(*shm_mbuf == MAP_FAILED) {
		shm_unlink(name);
		return -3;
	}

	// check buffer name just in case...
	if(strncmp(name, (**shm_mbuf).name, MBUFFER_MAX_NAME-1) != 0) {
		munmap(*shm_mbuf, total_size);
		shm_unlink(name);
		return -4;
	}
	
	return (int)(**shm_mbuf).size;
}

void shm_mbuffer_destroy(shm_mbuffer_t *shm_mbuf)
{
	char name[MBUFFER_MAX_NAME];
	int fd = (*shm_mbuf).fd;
	size_t fsize = (*shm_mbuf).fsize;
	
	memcpy(name, (*shm_mbuf).name, MBUFFER_MAX_NAME);
	
	munmap(shm_mbuf, fsize);
	shm_unlink(name);
}

void shm_mbuffer_close(shm_mbuffer_t *shm_mbuf)
{
	//char name[MBUFFER_MAX_NAME];
	//int fd = (*shm_mbuf).fd;
	size_t fsize = (*shm_mbuf).fsize;
	
	//memcpy(name, (*shm_mbuf).name, MBUFFER_MAX_NAME);
	
	munmap(shm_mbuf, fsize);
}
#if 0
void shm_mbuffer_put(shm_mbuffer_t *shm_mbuf, const void *data, size_t size)
{
	int pos;	
	sem_wait(&(*shm_mbuf).free);
	lock(&(*shm_mbuf).lock);
	pos = (*shm_mbuf).h;
	(*shm_mbuf).h = ((*shm_mbuf).h+1) % (*shm_mbuf).count;

	memcpy(((void*)shm_mbuf)+(size_t)(*shm_mbuf).mem+pos*(*shm_mbuf).size, data, size<(*shm_mbuf).size?size:(*shm_mbuf).size);
	
	unlock(&(*shm_mbuf).lock);
	sem_post(&(*shm_mbuf).used);
}

int shm_mbuffer_tryput(shm_mbuffer_t *shm_mbuf, const void *data, size_t size)
{
	int pos;	
	if(sem_trywait(&(*shm_mbuf).free) != 0)
		return -1;
	lock(&(*shm_mbuf).lock);
	pos = (*shm_mbuf).h;
	(*shm_mbuf).h = ((*shm_mbuf).h+1) % (*shm_mbuf).count;
	
	memcpy(((void*)shm_mbuf)+(size_t)(*shm_mbuf).mem+pos*(*shm_mbuf).size, data, size<(*shm_mbuf).size?size:(*shm_mbuf).size);

	unlock(&(*shm_mbuf).lock);
	sem_post(&(*shm_mbuf).used);
	return 0;
}

void shm_mbuffer_get(shm_mbuffer_t *shm_mbuf, void *data, size_t size)
{
	int pos;	
	sem_wait(&(*shm_mbuf).used);
	
	lock(&(*shm_mbuf).lock);
	pos = (*shm_mbuf).t;
	(*shm_mbuf).t = ((*shm_mbuf).t+1) % (*shm_mbuf).count;
	
	memcpy(data, ((void*)shm_mbuf) + (size_t)(*shm_mbuf).mem+pos*(*shm_mbuf).size, size<(*shm_mbuf).size?size:(*shm_mbuf).size);
	
	unlock(&(*shm_mbuf).lock);
	sem_post(&(*shm_mbuf).free);
}

#endif
void* shm_mbuffer_get_write(shm_mbuffer_t *shm_mbuf, mbuffer_key_t *key)
{
	void * ret = NULL;
	assert(shm_mbuf != NULL);

	sem_wait(&(*shm_mbuf).Wsem);

	lock(&(*shm_mbuf).Wlock);
		mbuffer_key_t tkey = getffsl((*shm_mbuf).Wfield) - 1;
		(*shm_mbuf).Wfield ^= (1 << tkey);
	unlock(&(*shm_mbuf).Wlock);

	ret = ((void*)shm_mbuf)+(size_t)(*shm_mbuf).mem+tkey*(*shm_mbuf).size;
	*key = tkey;	
	assert(tkey < 64);
	return ret;
}

void* shm_mbuffer_tryget_write(shm_mbuffer_t *shm_mbuf, mbuffer_key_t *key)
{
	void * ret = NULL;
	assert(shm_mbuf != NULL);
	
	if(sem_trywait(&(*shm_mbuf).Wsem) != 0)
		return NULL;

	lock(&(*shm_mbuf).Wlock);
		mbuffer_key_t tkey = getffsl((*shm_mbuf).Wfield) - 1;
		(*shm_mbuf).Wfield ^= (1 << tkey);
	unlock(&(*shm_mbuf).Wlock);

	ret = ((void*)shm_mbuf)+(uintptr_t)(*shm_mbuf).mem+(uintptr_t)tkey*(uintptr_t)(*shm_mbuf).size;
	*key = tkey;	
	assert(tkey < 64);
	return ret;
}


void shm_mbuffer_put_write(shm_mbuffer_t *shm_mbuf, const mbuffer_key_t key)
{
	assert(shm_mbuf != NULL);
	assert(key < 64);

	lock(&(*shm_mbuf).Rlock);
		(*shm_mbuf).Rfield |= (1 << key);
	unlock(&(*shm_mbuf).Rlock);
	sem_post(&(*shm_mbuf).Rsem);
}

void shm_mbuffer_discard_write(shm_mbuffer_t *shm_mbuf, const mbuffer_key_t key)
{
	assert(shm_mbuf != NULL);
	assert(key < 64);

	lock(&(*shm_mbuf).Wlock);
		(*shm_mbuf).Wfield |= (1 << key);
	unlock(&(*shm_mbuf).Wlock);
	sem_post(&(*shm_mbuf).Wsem);
}

void* shm_mbuffer_get_read(shm_mbuffer_t *shm_mbuf, mbuffer_key_t *key)
{
	void * ret = NULL;
	assert(shm_mbuf != NULL);

	sem_wait(&(*shm_mbuf).Rsem);

	lock(&(*shm_mbuf).Rlock);
		mbuffer_key_t tkey = getffsl((*shm_mbuf).Rfield) - 1;
		(*shm_mbuf).Rfield ^= (1 << tkey);
	unlock(&(*shm_mbuf).Rlock);

	ret = ((void*)shm_mbuf)+(size_t)(*shm_mbuf).mem+tkey*(*shm_mbuf).size;
	*key = tkey;	
	
	return ret;
}

void shm_mbuffer_put_read(shm_mbuffer_t *shm_mbuf, const mbuffer_key_t key)
{
	assert(shm_mbuf != NULL);
	assert(key < 64);

	lock(&(*shm_mbuf).Wlock);
		(*shm_mbuf).Wfield |= (1 << key);
	unlock(&(*shm_mbuf).Wlock);
	sem_post(&(*shm_mbuf).Wsem);
}

void shm_mbuff_put_read_zmq(void* data, void* hint)
{
	shm_mbuffer_t *shm_mbuf = (shm_mbuffer_t *)hint;
	mbuffer_key_t key = (data - (((void*)shm_mbuf)+(size_t)(*shm_mbuf).mem)) / (*shm_mbuf).size;
	
	shm_mbuffer_put_read(shm_mbuf, key);
}


