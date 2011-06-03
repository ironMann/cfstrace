#ifndef _SHM_CBUFFER_H
#define _SHM_CBUFFER_H

#define CBUFFER_MAX_NAME 128

typedef struct {
	void *mem;
	size_t size;
	size_t count;
	size_t h,t;

	pthread_spinlock_t lock;
	sem_t free;
	sem_t used;

	int fd;
	size_t fsize;

	char name[CBUFFER_MAX_NAME];
} __attribute__ ((aligned (16))) shm_cbuffer_t;


int shm_cbuffer_create(shm_cbuffer_t **shm_cbuf, const char *name, const size_t obj_size, const size_t count);

// return size of object slot (can be more than obj size created with), -1 on fail
int shm_cbuffer_open(shm_cbuffer_t **shm_cbuf, const char *name);

void shm_cbuffer_destroy(shm_cbuffer_t *shm_cbuf);

void shm_cbuffer_close(shm_cbuffer_t *shm_cbuf);

void shm_cbuffer_put(shm_cbuffer_t *shm_cbuf, const void *data, size_t size);

int shm_cbuffer_tryput(shm_cbuffer_t *shm_cbuf, const void *data, size_t size);

void shm_cbuffer_get(shm_cbuffer_t *shm_cbuf, void *data, size_t size);


#endif // _SHM_CBUFFER_H
