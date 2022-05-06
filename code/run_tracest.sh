#!/bin/bash

thread=8   # set the number of running threads
chunksize=8 # set the chunk size of straid to (chunksize * KB)

sed -in-place -e '/^#define NUM_THREADS/ c \#define NUM_THREADS ('${thread}")" ./include/define.h
sed -in-place -e '/^#define SCHUNK_SIZE/ c \#define SCHUNK_SIZE ('${chunksize}" * KB)" ./include/define.h
make -j4

echo "" > ./results/ST_trace_results.txt

numactl --cpubind=0 --membind=0 ./bin/trace_st
