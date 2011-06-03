#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
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

//#define init_lock(l) do { pthread_spin_init(l, PTHREAD_PROCESS_SHARED); } while(0)
//#define lock(l)      do { pthread_spin_lock(l); } while(0)
//#define unlock(l)    do { pthread_spin_unlock(l); } while(0)

#define init_lock(l) do { sharedMemoryMutexInit(l); } while(0)
#define lock(l)      do { pthread_mutex_lock(l); } while(0)
#define unlock(l)    do { pthread_mutex_unlock(l); } while(0)

#define ALIGN_ON 16

// dangerous!!!
#define MBUFFER_READ_TIMEOUT_MSEC  (uint64_t)120000
#define MBUFFER_WRITE_TIMEOUT_MSEC (uint64_t)120000
#define MBUFFER_STALE_CHECK_SEC              20

static inline uint64_t timestamp()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint64_t)tv.tv_usec + (uint64_t)1000000 * (uint64_t)tv.tv_sec;
}

void* stale_op_checker(void *data)
{
	shm_mbuffer_t *self = (shm_mbuffer_t*)data;
	int i;

	sleep(MBUFFER_STALE_CHECK_SEC);

	while(1) {
		const uint64_t ctime = timestamp();

		lock(&(*self).Wlock);
		lock(&(*self).Rlock);

		for(i=0; i<sizeof(int)*8; i++) {
			if(((*self).wstart[i] !=0) && ((ctime - (*self).wstart[i]) > MBUFFER_WRITE_TIMEOUT_MSEC*1000)) {
				(*self).Wfield |= ((int)1 << (int)i);
				(*self).Rfield &= ~((int)1 << (int)i);
				(*self).rstart[i] = 0;
				(*self).wstart[i] = 0;
				sem_post(&(*self).Wsem);

				printf("Write stale detected!!! key: %d\n", i);
			}
		}

		for(i=0; i<sizeof(int)*8; i++) {
			if(((*self).rstart[i] !=0) && ((ctime - (*self).rstart[i]) > MBUFFER_READ_TIMEOUT_MSEC*1000)) {
				(*self).Wfield |= ((int)1 << (int)i);
				(*self).Rfield &= ~((int)1 << (int)i);
				(*self).rstart[i] = 0;
				(*self).wstart[i] = 0;
				sem_post(&(*self).Wsem);

				printf("Read stale detected!!! key: %d\n", i);
			}
		}

		int semval;
		sem_getvalue(&(*self).Wsem, &semval);
		if(semval > sizeof(int)*8)
			printf("********ERROR: Wsem on %s has value: %d\n", (*self).name, semval);

		unlock(&(*self).Rlock);
		unlock(&(*self).Wlock);

		sleep(MBUFFER_STALE_CHECK_SEC);
	};

	pthread_exit(NULL);
	return NULL;
}


void printX(uint64_t x)
{
	printf("%016llX\n", x);
}

static void sharedMemoryMutexInit(pthread_mutex_t * GlobalMutex)
{
	pthread_mutexattr_t oMutexAttribute;
	pthread_mutexattr_init(&oMutexAttribute);
	if(0 == pthread_mutexattr_setrobust_np(&oMutexAttribute, PTHREAD_MUTEX_ROBUST_NP)) {
		if(0 == pthread_mutexattr_setpshared(&oMutexAttribute, PTHREAD_PROCESS_SHARED)) {
			if(0 == pthread_mutexattr_settype(&oMutexAttribute, PTHREAD_MUTEX_ERRORCHECK)) {
				if(0 == pthread_mutex_init(GlobalMutex, &oMutexAttribute)) {
					//printf("GlobalMutex initialized.\n");
				}
				else {
					printf("pthread_mutex_init Failure.\n");
				}
			}
			else {
				printf("pthread_mutexattr_settype Failure.\n");
			}
		}
		else {
			printf("pthread_mutexattr_setpshared Failure.\n");
		}
	}
	else {
		printf("pthread_mutexattr_setrobust_np Failure.\n");
	}

	pthread_mutexattr_destroy(&oMutexAttribute);
}

inline unsigned int getffsl(int x)
{
	return ffs(x);
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
	count = sizeof(int)*8; //for now
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
	(**shm_mbuf).Rfield = 0x0;
	(**shm_mbuf).Wfield = ~0x0;
	strncpy((**shm_mbuf).name, name, MBUFFER_MAX_NAME-1);

	pthread_create(&(**shm_mbuf).thousekeep, NULL, stale_op_checker, *shm_mbuf);

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
		(*shm_mbuf).Wfield ^= ((int)1 << (int)tkey);
		(*shm_mbuf).wstart[tkey] = timestamp();
	unlock(&(*shm_mbuf).Wlock);

	ret = ((void*)shm_mbuf)+(size_t)(*shm_mbuf).mem+tkey*(*shm_mbuf).size;
	*key = tkey;
	assert(tkey < sizeof(int)*8);
	return ret;
}

void* shm_mbuffer_tryget_write(shm_mbuffer_t *shm_mbuf, mbuffer_key_t *key)
{
	void * ret = NULL;
	assert(shm_mbuf != NULL);

	if(sem_trywait(&(*shm_mbuf).Wsem) != 0) {
		*key = -1;
		return NULL;
	}

	lock(&(*shm_mbuf).Wlock);
		mbuffer_key_t tkey = getffsl((*shm_mbuf).Wfield) - 1;
		(*shm_mbuf).Wfield ^= ((int)1 << (int)tkey);
		(*shm_mbuf).wstart[tkey] = timestamp();
	unlock(&(*shm_mbuf).Wlock);

	ret = ((void*)shm_mbuf)+(uintptr_t)(*shm_mbuf).mem+(uintptr_t)tkey*(uintptr_t)(*shm_mbuf).size;
	*key = tkey;
	assert(tkey < sizeof(int)*8);
	return ret;
}


void shm_mbuffer_put_write(shm_mbuffer_t *shm_mbuf, const mbuffer_key_t key)
{
	assert(shm_mbuf != NULL);
	assert(key < sizeof(int)*8);

	lock(&(*shm_mbuf).Rlock);
		if((*shm_mbuf).wstart[key] == 0) { // not enoguh to detect stale write?!
			unlock(&(*shm_mbuf).Rlock);
			return;
		}
		(*shm_mbuf).Rfield |= ((int)1 << (int)key);
		(*shm_mbuf).wstart[key] = 0;
	unlock(&(*shm_mbuf).Rlock);
	sem_post(&(*shm_mbuf).Rsem);
}

void shm_mbuffer_discard_write(shm_mbuffer_t *shm_mbuf, const mbuffer_key_t key)
{
	assert(shm_mbuf != NULL);
	assert(key < sizeof(int)*8);

	lock(&(*shm_mbuf).Wlock);
		if((*shm_mbuf).wstart[key] == 0) { // not enoguh to detect stale write?!
			unlock(&(*shm_mbuf).Wlock);
			return;
		}
		(*shm_mbuf).Wfield |= ((int)1 << (int)key);
		(*shm_mbuf).wstart[key] = 0;
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
		(*shm_mbuf).Rfield ^= ((int)1 << (int)tkey);
		(*shm_mbuf).rstart[tkey] = timestamp();
	unlock(&(*shm_mbuf).Rlock);

	ret = ((void*)shm_mbuf)+(uintptr_t)(*shm_mbuf).mem+ (uintptr_t)tkey*(uintptr_t)(*shm_mbuf).size;
	*key = tkey;
	assert(tkey < sizeof(int)*8);

	return ret;
}

void shm_mbuffer_put_read(shm_mbuffer_t *shm_mbuf, const mbuffer_key_t key)
{
	assert(shm_mbuf != NULL);
	assert(key < sizeof(int)*8);

	lock(&(*shm_mbuf).Wlock);
		if((*shm_mbuf).rstart[key] == 0) { // not enoguh to detect stale read?!
			unlock(&(*shm_mbuf).Wlock);
			return;
		}
		(*shm_mbuf).Wfield |= ((int)1 << (int)key);
		(*shm_mbuf).rstart[key] = 0;
	unlock(&(*shm_mbuf).Wlock);
	sem_post(&(*shm_mbuf).Wsem);
}

void shm_mbuff_put_read_zmq(void* data, void* hint)
{
	shm_mbuffer_t *shm_mbuf = (shm_mbuffer_t *) hint;
	mbuffer_key_t key = ((uintptr_t)data - (((uintptr_t)shm_mbuf)+(uintptr_t)(*shm_mbuf).mem)) / (uintptr_t)(*shm_mbuf).size;
	shm_mbuffer_put_read(shm_mbuf, key);
}


#if 0
int main()
{
	shm_mbuffer_t *bb;
	printf("start\n");
	int i; uint64_t a;
	for(i=0; i<64; i++) {
		a = ((uint64_t)1 << i);
		printX(a);
		printX(getffsl(a));
	}


	shm_mbuffer_create(&bb, "/test", sizeof(int), 444);
	mbuffer_key_t k;
	do {
		void * addr = shm_mbuffer_get_write(bb, &k);

		shm_mbuffer_put_write(bb, k);

		addr = shm_mbuffer_get_read(bb, &k);

		printf("addr %X, key %d\n", addr, k);
	}
	while(1);
	return 0;
}
#endif

