#!/bin/bash

export LD_PRELOAD=$1
echo $1

gcc -fPIC -shared -o libread.so shm_mbuffer.c read.c -lpthread -lzmq -ldl -O3
gcc -o receiver receiver.c sqlite_adapter.c sqlite3.c -lzmq -ldl -O3
gcc -o collect shm_cbuffer.c collect.c shm_mbuffer.c -lzmq -ldl -O3


export LD_PRELOAD=
