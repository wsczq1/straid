#!/bin/bash
thread=(1 2 4 8 16 32 64)
chunksize=64 # set the chunk size to (val * KB)

IOsize="PARTSIZE" # set the benchmark to test partial-stripe write
# IOsize="FULLSIZE" # set the benchmark to test full-stripe write

sed -in-place -e '/^#define SCHUNK_SIZE/ c \#define SCHUNK_SIZE ('${chunksize}" * KB)" ./include/define.h

echo "" >./results/bench_out.txt
echo "[Average Throughput | Average Lat | 50th Lat | 90th Lat | 99th Lat  | 999th Lat ]" >>./results/bench_out.txt
echo "[Sequential-Read  Random-Read  Sequential-Write  Random-Write]" >>./results/bench_out.txt

for th in ${thread[@]}; do
    sed -in-place -e '/^#define NUM_THREADS/ c \#define NUM_THREADS ('${th}")" ./include/define.h
    make -j4
    sleep 3
    ./bin/bench ${IOsize}
    sleep 3
    # echo "" >> bench_out.txt # Newline
done
