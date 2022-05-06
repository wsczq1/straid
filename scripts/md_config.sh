#!/bin/sh

# Assign CHUNKSIZE with the length of the stripe chunk size
CHUNKSIZE="64K"

# Assign mdadm with SSD device directories
# the [-n] parameter should be modified to the number of used SSD devices in the array
mdadm -v -C /dev/md5 -l 5 -n 6 -c ${CHUNKSIZE} --force /dev/nvme0n1 /dev/nvme1n1 /dev/nvme2n1 /dev/nvme3n1 /dev/nvme4n1 /dev/nvme5n1

sleep 3
cat /proc/mdstat

# Assign cachesize, the default stripe cache size of MD is 2048
# Assign numworkers, the default number of workers of MD is 0
cachesize=32768
numworkers=64

echo "default MD Settings:"
cat /sys/block/md5/md/group_thread_cnt 
cat /sys/block/md5/md/stripe_cache_size 

echo ${numworkers} > /sys/block/md5/md/group_thread_cnt 
echo ${cachesize} > /sys/block/md5/md/stripe_cache_size 

echo "Optimized MD Settings:"
cat /sys/block/md5/md/group_thread_cnt 
cat /sys/block/md5/md/stripe_cache_size 