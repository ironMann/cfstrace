#ifndef _SHM_MBUFFER_H
#define _SHM_MBUFFER_H

#define MBUFFER_MAX_NAME 128

typedef uintptr_t mbuffer_key_t;

typedef struct {
	uintptr_t mem;
	size_t size;
	size_t count;

	int fd;
	size_t fsize;

	char name[MBUFFER_MAX_NAME];

	pthread_t thousekeep;

	uintptr_t Wlog;
	int Hw, Tw;
	pthread_mutex_t Wlock;
	sem_t Wsem;
	uintptr_t Wopstart;

	uintptr_t Rlog;
	int Hr, Tr;
	pthread_mutex_t Rlock;
	sem_t Rsem;
	uintptr_t Ropstart;

} __attribute__ ((aligned (16))) shm_mbuffer_t;


int shm_mbuffer_create(shm_mbuffer_t **shm_cbuf, const char *name, const size_t obj_size, /*const*/ size_t count);

// return size of object slot (can be more than obj size created with), -1 on fail
int shm_mbuffer_open(shm_mbuffer_t **shm_mbuf, const char *name);

void shm_mbuffer_destroy(shm_mbuffer_t *shm_cbuf);

void shm_mbuffer_close(shm_mbuffer_t *shm_cbuf);

//void shm_mbuffer_put(shm_mbuffer_t *shm_cbuf, const void *data, size_t size);
//int shm_mbuffer_tryput(shm_mbuffer_t *shm_cbuf, const void *data, size_t size);
//void shm_mbuffer_get(shm_mbuffer_t *shm_cbuf, void *data, size_t size);


void* shm_mbuffer_get_write(shm_mbuffer_t *shm_mbuf, mbuffer_key_t *key);
void* shm_mbuffer_tryget_write(shm_mbuffer_t *shm_mbuf, mbuffer_key_t *key);
void shm_mbuffer_put_write(shm_mbuffer_t *shm_mbuf, const mbuffer_key_t key);
void shm_mbuffer_discard_write(shm_mbuffer_t *shm_mbuf, const mbuffer_key_t key);

void* shm_mbuffer_get_read(shm_mbuffer_t *shm_mbuf, mbuffer_key_t *key);
void shm_mbuffer_put_read(shm_mbuffer_t *shm_mbuf, const mbuffer_key_t key);

void shm_mbuff_put_read_zmq(void* data, void* hint);

#endif // _SHM_MBUFFER_H
