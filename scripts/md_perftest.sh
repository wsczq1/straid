#! /bin/bash

blocksize="64K" # The I/O request size of FIO   
thread=(1 2 4 8 16 32 64) # The number of IO-s threads of FIO   

echo "" > fio_results.log

sed -in-place -e '/^ioengine=/ c \ioengine=sync' raid.fio
sed -in-place -e '/^numjobs=/ c \numjobs=1' raid.fio
sed -in-place -e '/^iodepth=/ c \iodepth=1' raid.fio
sed -in-place -e '/^bs=/ c \bs='${blocksize} raid.fio
sed -in-place -e '/^runtime=/ c \runtime=10' raid.fio

echo "================ Start Thread Test ================"

for th in ${thread[@]}; do
    echo 3 > /proc/sys/vm/drop_caches 
    sleep 1
    sed -i '/^numjobs=/ c numjobs='${th} raid.fio

    numactl --cpubind=0 --membind=0 fio raid.fio >> fio_results.log
    echo ">>>>> RAID-${rm}: Thread ${th} End <<<<<"

    sleep 1
done

echo "================ Test End ================"
