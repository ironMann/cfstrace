#!/bin/bash

export LD_PRELOAD=$1
echo $1

colorgcc -fPIC -shared -o libread.so read.c shm_cbuffer.c -lpthread -lzmq -ldl -O3
colorgcc -o receiver receiver.c sqlite_adapter.c sqlite3.c -lzmq -ldl -O3
colorgcc -o collect shm_cbuffer.c collect.c -lzmq -ldl -O3


export LD_PRELOAD=
