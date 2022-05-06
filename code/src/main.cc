#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <atomic>
#include "concurrentqueue.h"

#include "tools.h"
#include "worker.h"
#include "encode.h"
#include "decode.h"
#include "logfiles.h"
#include "storageMod.h"
#include "define.h"
#include "metadata.h"

using namespace std;

extern atomic_uint64_t All_Write_Data;
extern atomic_uint64_t All_Read_Data;
extern atomic_uint64_t Batch_Count;
extern atomic_uint64_t Block_Count;
extern atomic_uint64_t Cache_Hit;
extern atomic_uint64_t Cache_Miss;
extern atomic_bool proc_end_flag;

extern StorageMod *GloStor;

int main(int argc, char *argv[])
{
    cout << "Init System" << endl;
    assert(NUM_DEV == (DATACHUNK_NUM + PARITYCHUNK_NUM));
    assert(PARITYCHUNK_NUM <= 2);
    assert(DATACHUNK_NUM > 1);
    cout << dec << "Number of SSDs: " << NUM_DEV << " | Data Chunks: " << DATACHUNK_NUM << " | Parity Chunks: " << PARITYCHUNK_NUM << endl;

    srand(time(0));
    DropCaches(3);
    sleep(1);

    int cpus = sysconf(_SC_NPROCESSORS_CONF);
    printf("This System has %d Processors\n", cpus);

    // paths of raw SSD devices or SSD partations
    string lfile0 = "/dev/nvme0n1p4";
    string lfile1 = "/dev/nvme1n1p4";
    string lfile2 = "/dev/nvme2n1p4";
    string lfile3 = "/dev/nvme3n1p4";
    string lfile4 = "/dev/nvme4n1p4";
    string lfile5 = "/dev/nvme5n1p4";
    vector<string> v_fileset{lfile0, lfile1, lfile2, lfile3, lfile4, lfile5};
    assert(NUM_DEV <= v_fileset.size());

    vector<int> v_logfd;
    for (size_t i = 0; i < v_fileset.size(); i++)
    {
        int fd = open(v_fileset[i].c_str(), O_RDWR | O_DIRECT | O_TRUNC);
        assert(fd != -1);
        v_logfd.emplace_back(fd);
    }

    V_DevFiles v_stdFiles;
    for (size_t i = 0; i < (NUM_DEVFILES); i++)
    {
        DevFile *devfile = new DevFile(i, v_fileset[i], v_logfd[i], 0, STRA_SPACE_LEN);
        v_stdFiles.emplace_back(devfile);
    }

    uint64_t user_start_offset = 0;
    uint64_t user_end_offset = USER_SPACE_LEN;

    MetaMod metamod(user_start_offset, user_end_offset, &v_stdFiles);
    StorageMod storagemod(&v_stdFiles, &metamod);
    GloStor = &storagemod;

    printf("Generating Workloads for write\n");
    vector<char *> workloadw_buf;
    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        char *buf;
        int ret = posix_memalign((void **)&buf, ALIGN_SIZE, (DATASET_SIZE / NUM_THREADS) + MB);
        assert(ret == 0);
        int init_int = rand() % 0xff;
        memset(buf, init_int, (DATASET_SIZE / NUM_THREADS));
        // cout << "Workload init buffer: " << hex << init_int << endl;
        workloadw_buf.emplace_back(buf);
    }

    cout << dec << ">> START multi-worker with " << NUM_THREADS << " write workers" << endl;
    {
        printf(">>> Concurrent Write Load\n");
        tic(9);
        pthread_t wtids[NUM_THREADS];
        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            Worker_Info *info = (Worker_Info *)malloc(sizeof(Worker_Info));
            info->io_dir = true; // Write IO
            info->is_rand = false;
            info->thread_id = i;
            info->io_size = IO_SIZE;
            info->offset = o_align(i * (DATASET_SIZE / NUM_THREADS), IO_SIZE);
            info->count = (DATASET_SIZE / IO_SIZE / NUM_THREADS);
            info->loop = LOOP;
            info->storagemod = &storagemod;

            info->workload_buf = workloadw_buf.at(i);
            info->workload_len = (DATASET_SIZE / NUM_THREADS);

            int ret = pthread_create(&wtids[i], NULL, thread_worker, info);
            if (ret != 0)
            {
                printf("pthread_create error: error_code=%d\n", ret);
            }
        }
        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            pthread_join(wtids[i], NULL);
        }
        double timer = toc(9);
        print_throughtput(DATASET_SIZE * LOOP, LOOP * DATASET_SIZE / IO_SIZE, timer, "DISK WRITE LOAD END");
        printf("ALL write data: %lld, All read data: %lld\n", All_Write_Data.load() / MB, All_Read_Data.load() / MB);
        printf("Batch count: %ld, Block count: %ld\n", Batch_Count.load(), Block_Count.load());
        printf("cache hit: %ld, cache miss: %ld\n", Cache_Hit.load(), Cache_Miss.load());
        All_Write_Data.store(0);
        All_Read_Data.store(0);
        Batch_Count.store(0);
        Block_Count.store(0);
    }

    {
        printf(">>> Concurrent Read Load\n");
        vector<char *> workloadr_buf;
        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            char *buf;
            int ret = posix_memalign((void **)&buf, ALIGN_SIZE, (DATASET_SIZE / NUM_THREADS));
            assert(ret == 0);
            memset(buf, 0, (DATASET_SIZE / NUM_THREADS));
            workloadr_buf.emplace_back(buf);
        }
        cout << dec << ">> START multi-worker with " << NUM_THREADS << " read workers" << endl;
        pthread_t rtids[NUM_THREADS];
        uint64_t iosize = IO_SIZE;
        tic(9);
        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            Worker_Info *info = (Worker_Info *)malloc(sizeof(Worker_Info));
            info->io_dir = false; // Read IO
            info->is_rand = false;
            info->thread_id = i;
            info->io_size = iosize;
            info->offset = o_align(i * (DATASET_SIZE / NUM_THREADS), iosize);
            info->count = (DATASET_SIZE / iosize / NUM_THREADS);
            info->loop = LOOP;
            info->storagemod = &storagemod;
            info->is_degr_read = false;

            info->workload_buf = workloadr_buf.at(i);
            info->workload_len = (DATASET_SIZE / NUM_THREADS);

            int ret = pthread_create(&rtids[i], NULL, thread_worker, info);
            if (ret != 0)
            {
                printf("pthread_create error: error_code=%d\n", ret);
            }
        }
        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            pthread_join(rtids[i], NULL);
        }
        double timer = toc(9);
        print_throughtput(DATASET_SIZE * LOOP, LOOP * DATASET_SIZE / iosize, timer, "SEQ READ LOAD END");
        printf("ALL write data: %lld, All read data: %lld\n", All_Write_Data.load() / MB, All_Read_Data.load() / MB);
        All_Write_Data.store(0);
        All_Read_Data.store(0);
    }

    sleep(1);
    exit(0);
    return 0;
}
