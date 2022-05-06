#ifndef WORKER_H
#define WORKER_H

#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <sys/types.h>

#include <cds/init.h>
#include <cds/threading/model.h>
#include <cds/container/feldman_hashmap_dhp.h>
#include <cds/gc/hp.h>

#include "queues.h"
#include "define.h"
#include "logfiles.h"
#include "metadata.h"
#include "storageMod.h"

using namespace std;

atomic_uint64_t SeqW_Offset(0);
atomic_bool wait_array[NUM_THREADS];
atomic_uint64_t pos_array[NUM_THREADS];

struct Worker_Info
{
    int thread_id; // Thread ID

    UserQueue *userqueue;
    StorageMod *storagemod;

    bool io_dir;  // IO direction
    bool is_rand; // is random access
    bool is_degr_read;

    uint64_t io_size; // io size
    int count;        // io conut in a loop
    int loop;         // num of loops

    uint64_t offset;

    char *workload_buf; // pre-assigned workload buf
    uint64_t workload_len;
};

void *thread_worker(void *worker_info);
uint64_t Rand_offset_align(uint64_t baseoff, uint64_t max_offset, uint64_t align);

void *thread_worker(void *worker_info)
{
    cds::threading::Manager::attachThread();

    Worker_Info *info = (Worker_Info *)worker_info;
    UserQueue *userqueue = info->userqueue;
    StorageMod *storagemod = info->storagemod;

    bool iodir = info->io_dir;
    bool is_degr_read = info->is_degr_read;
    int thread_id = info->thread_id;
    uint64_t base_offset = info->offset;
    uint64_t iosize = info->io_size;
    int count = info->count;
    int total_loop = info->loop;

    cpu_set_t mask;                                           // CPU mask
    cpu_set_t get;                                            
    CPU_ZERO(&mask);                                          
    CPU_SET(thread_id, &mask);                                
    // if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1) // set CPU affinity
    // {
    //     printf("warning: could not set CPU affinity, continuing...\n");
    //     exit(-1);
    // }

    wait_array[thread_id].store(false);
    pos_array[thread_id].store(0);

    char *workload_buf = info->workload_buf;
    uint64_t workload_len = info->workload_len;

    // printf(" Worker Thread: %d Start\n", thread_id);

    uint64_t total_data = 0;
    tic(10 + thread_id);
    if (iodir == true)
    {
        for (int loop = 0; loop < total_loop; loop++)
        {
            uint64_t last_buf_start = 0;
            for (int i = 0; i < count; i++)
            {
                if (iosize < BLK_SIZE || iosize % BLK_SIZE != 0)
                {
                    iosize += BLK_SIZE - (iosize % BLK_SIZE);
                }
                char *buf_start = workload_buf + last_buf_start;

                uint64_t offset = 0;
                if (info->is_rand)
                {
                    offset = Rand_offset_align(base_offset, DATASET_SIZE, iosize);
                }
                else
                {
                    // offset = base_offset + (uint64_t)(i * iosize);
                    offset = SeqW_Offset.fetch_add(1) * iosize;
                }

                lat_tic(thread_id);
                UIO_Info iometa(thread_id, true, buf_start, offset, iosize);
                storagemod->raid_write_direct(iometa);
                lat_toc(thread_id);

                last_buf_start += iosize;
                total_data += iosize;
            }
        }
    }
    else
    {
        for (int loop = 0; loop < total_loop; loop++)
        {
            uint64_t read_offset = 0;
            uint64_t read_length = iosize;
            uint64_t last_buf_start = 0;

            for (int i = 0; i < count; i++)
            {
                if (!is_degr_read)
                {
                    if (iosize < BLK_SIZE || iosize % BLK_SIZE != 0)
                    {
                        iosize += BLK_SIZE - (iosize % BLK_SIZE);
                    }
                    char *buf_start = workload_buf + last_buf_start;

                    uint64_t offset = 0;
                    if (info->is_rand)
                    {
                        offset = Rand_offset_align(base_offset, DATASET_SIZE / NUM_THREADS, iosize);
                    }
                    else
                    {
                        offset = base_offset + (uint64_t)(i * iosize);
                    }

                    lat_tic(thread_id);
                    bool is_write = false;
                    UIO_Info iometa(thread_id, is_write, buf_start, offset, iosize);
                    uint64_t ret = storagemod->raid_read(iometa);
                    lat_toc(thread_id);
                }

                read_offset += iosize;
                last_buf_start += iosize;
                total_data += iosize;
            }
        }
    }

    double time = toc(10 + thread_id);
    // printf("Thread: %d End | Time used: %.2f ms | Total: %ld MB\n", thread_id, (time / 1000 / 1000), (total_data / 1024 / 1024));

    return NULL;
}

uint64_t Rand_offset_align(uint64_t baseoff, uint64_t max_offset, uint64_t align)
{
    uint64_t ret = 0;
    uint64_t max = max_offset / align - 1;
    ret = (rand() % max) * align + baseoff;
    return ret;
}

#endif