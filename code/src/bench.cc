#include <iostream>
#include <fstream>
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

#include <cds/init.h>
#include <cds/threading/model.h>
#include <cds/container/feldman_hashmap_dhp.h>
#include <cds/gc/hp.h>

#include "tools.h"
#include "worker.h"
#include "encode.h"
#include "decode.h"
#include "logfiles.h"
#include "storageMod.h"
#include "metadata.h"


using namespace std;

extern atomic_uint64_t All_Write_Data;
extern atomic_uint64_t All_Read_Data;

extern atomic_uint64_t Cache_Hit;
extern atomic_uint64_t Cache_Miss;

extern StorageMod *GloStor;

vector<float> band_results;
vector<float> iops_results;
vector<uint64_t> latency_results[4];
int G_IOSIZE = 0;

extern atomic_bool proc_end_flag;

void test_SeqWrite(int testid, StorageMod *storagemod, vector<char *> *workloadw_buf);
void test_RandWrite(int testid, StorageMod *storagemod, vector<char *> *workloadw_buf);
void test_SeqRead(int testid, StorageMod *storagemod, vector<char *> *workloadr_buf);
void test_RandRead(int testid, StorageMod *storagemod, vector<char *> *workloadr_buf);

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        cout << "Need I/O size (e.g., 64K)" << endl;
        cout << "or input 'FULLSIZE' for testing full-stripe write performance" << endl;
        cout << "or input 'PARTSIZE' for testing partial-stripe write performance" << endl;

        exit(0);
    }
    else
    {
        char ck = argv[1][strlen(argv[1]) - 1];
        if (ck == 'k' || ck == 'K')
        {
            argv[1][strlen(argv[1]) - 1] = '\n';
            G_IOSIZE = atoi(argv[1]) * 1024;
        }

        if (strcmp(argv[1], "FULLSIZE") == 0)
        {
            G_IOSIZE = FULLSIZE;
        }
        else if (strcmp(argv[1], "PARTSIZE") == 0)
        {
            G_IOSIZE = PARTSIZE;
        }
    }

    cout << "Init System" << endl;
    cout << dec << "Number of SSDs: " << NUM_DEV << " | Data Chunks: " << DATACHUNK_NUM << " | Parity Chunks: " << PARITYCHUNK_NUM << endl;
    cout << dec << "Number of Workers: " << NUM_THREADS << endl;
    cout << dec << "IO size: " << G_IOSIZE / 1024 << "K" << endl;
    assert(NUM_DEV == (DATACHUNK_NUM + PARITYCHUNK_NUM));
    srand(time(0));

    string lfile0 = "/dev/nvme0n1p4";
    string lfile1 = "/dev/nvme1n1p4";
    string lfile2 = "/dev/nvme2n1p4";
    string lfile3 = "/dev/nvme3n1p4";
    string lfile4 = "/dev/nvme4n1p4";
    string lfile5 = "/dev/nvme5n1p4";

    // string lfile0 = "/dev/ram0";
    // string lfile1 = "/dev/ram1";
    // string lfile2 = "/dev/ram2";
    // string lfile3 = "/dev/ram3";
    // string lfile4 = "/dev/ram4";
    // string lfile5 = "/dev/ram5";
    vector<string> v_fileset{lfile0, lfile1, lfile2, lfile3, lfile4, lfile5};
    assert(NUM_DEV <= v_fileset.size());

    cout << "Open Files" << endl;
    vector<int> v_logfd;
    for (size_t i = 0; i < v_fileset.size(); i++)
    {
        int fd = open(v_fileset[i].c_str(), O_RDWR | O_DIRECT | O_TRUNC);
        assert(fd != -1);
        v_logfd.emplace_back(fd);
    }

    cout << "Generating DevFile" << endl;
    V_DevFiles v_stdFiles;
    for (size_t i = 0; i < (NUM_DEVFILES); i++)
    {
        DevFile *devfile = new DevFile(i, v_fileset[i], v_logfd[i], 0, STRA_SPACE_LEN);
        v_stdFiles.emplace_back(devfile);
    }

    uint64_t user_start_offset = 0;
    uint64_t user_end_offset = USER_SPACE_LEN;
    cout << "Generating MetaMod" << endl;
    MetaMod metamod(user_start_offset, user_end_offset, &v_stdFiles);
    cout << "Generating StorageMod" << endl;
    StorageMod storagemod(&v_stdFiles, &metamod);
    GloStor = &storagemod;

    cout << "Generating Write Buffer" << endl;
    vector<char *> workloadw_buf;
    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        char *buf;
        int ret = posix_memalign((void **)&buf, ALIGN_SIZE, (DATASET_SIZE / NUM_THREADS));
        assert(ret == 0);
        int init_int = rand() % 0xff;
        memset(buf, init_int, (DATASET_SIZE / NUM_THREADS));
        // cout << "Workload init buffer: " << hex << init_int << endl;
        workloadw_buf.emplace_back(buf);
    }

    vector<char *> workloadr_buf;
    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        char *buf;
        int ret = posix_memalign((void **)&buf, ALIGN_SIZE, (DATASET_SIZE / NUM_THREADS));
        assert(ret == 0);
        workloadr_buf.emplace_back(buf);
    }

    cout << "Test Start" << endl;
    {
        test_SeqRead(0, &storagemod, &workloadw_buf);
        test_RandRead(1, &storagemod, &workloadw_buf);
        test_SeqWrite(2, &storagemod, &workloadw_buf);
        test_RandWrite(3, &storagemod, &workloadw_buf);

        ofstream outfile;
        outfile.open("./results/bench_out.txt", ios::app | ios::ate);
        
        outfile << "Thread " << NUM_THREADS << "\t";

        for (size_t i = 0; i < band_results.size(); i++)
        {
            if (i % 4 == 0)
            {
                cout << endl;
            }
            cout << band_results[i] << "\t";
            outfile << band_results[i] << "\t";
        }
        cout << endl;
        outfile << "\t|\t";

        for (size_t i = 0; i < iops_results.size(); i++)
        {
            if (i % 4 == 0)
            {
                cout << endl;
            }
            cout << iops_results[i] << "\t";
            outfile << iops_results[i] << "\t";
        }
        cout << endl;
        outfile << "\t|\t";

        // Average latency
        for (size_t i = 0; i < 4; i++)
        {
            cout << latency_results[i].at(0) << "\t";
            outfile << latency_results[i].at(0) << "\t";
        }
        cout << endl;
        outfile << "\t|\t";

        // 50th latency
        for (size_t i = 0; i < 4; i++)
        {
            cout << latency_results[i].at(1) << "\t";
            outfile << latency_results[i].at(1) << "\t";
        }
        cout << endl;
        outfile << "\t|\t";

        // 90th latency
        for (size_t i = 0; i < 4; i++)
        {
            cout << latency_results[i].at(3) << "\t";
            outfile << latency_results[i].at(3) << "\t";
        }
        cout << endl;
        outfile << "\t|\t";

        // 99th latency
        for (size_t i = 0; i < 4; i++)
        {
            cout << latency_results[i].at(4) << "\t";
            outfile << latency_results[i].at(4) << "\t";
        }
        cout << endl;
        outfile << "\t|\t";

        // 999th latency
        for (size_t i = 0; i < 4; i++)
        {
            cout << latency_results[i].at(5) << "\t";
            outfile << latency_results[i].at(5) << "\t";
        }
        cout << endl;
        outfile << "\t|\t";

        outfile << endl;
        outfile.close();
    }

    sleep(1);
    exit(0);
    return 0;
}

void test_SeqWrite(int testid, StorageMod *storagemod, vector<char *> *workloadw_buf)
{
    cout << "test_SeqWrite" << endl;
    tic(9);
    pthread_t wtids[NUM_THREADS];
    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        Worker_Info *info = (Worker_Info *)malloc(sizeof(Worker_Info));
        info->io_dir = true;
        info->is_rand = false;
        info->thread_id = i;
        info->io_size = G_IOSIZE;
        info->offset = o_align(i * (DATASET_SIZE / NUM_THREADS), G_IOSIZE);
        info->count = (DATASET_SIZE / G_IOSIZE / NUM_THREADS);
        info->loop = LOOP;
        info->storagemod = storagemod;

        info->workload_buf = workloadw_buf->at(i);
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
    auto ret = print_throughtput(DATASET_SIZE * LOOP, LOOP * DATASET_SIZE / G_IOSIZE, timer, "SEQ WRITE LOAD END");
    All_Write_Data.store(0);
    All_Read_Data.store(0);

    band_results.emplace_back(ret.first);
    iops_results.emplace_back(ret.second);

    vector<uint64_t> *lat_results = show_IOLat(NUM_THREADS);
    for (size_t i = 0; i < lat_results->size(); i++)
    {
        latency_results[testid].emplace_back(lat_results->at(i));
    }
    clear_IOLat();
}

void test_RandWrite(int testid, StorageMod *storagemod, vector<char *> *workloadw_buf)
{
    cout << "test_RandWrite" << endl;
    tic(9);
    pthread_t wtids[NUM_THREADS];
    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        Worker_Info *info = (Worker_Info *)malloc(sizeof(Worker_Info));
        info->io_dir = true;
        info->is_rand = true;
        info->thread_id = i;
        info->io_size = G_IOSIZE;
        info->offset = o_align(i * (DATASET_SIZE / NUM_THREADS), G_IOSIZE);
        info->count = (DATASET_SIZE / G_IOSIZE / NUM_THREADS);
        info->loop = LOOP;
        info->storagemod = storagemod;

        info->workload_buf = workloadw_buf->at(i);
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
    auto ret = print_throughtput(DATASET_SIZE * LOOP, LOOP * DATASET_SIZE / G_IOSIZE, timer, "RAND WRITE LOAD END");
    All_Write_Data.store(0);
    All_Read_Data.store(0);

    band_results.emplace_back(ret.first);
    iops_results.emplace_back(ret.second);

    vector<uint64_t> *lat_results = show_IOLat(NUM_THREADS);
    for (size_t i = 0; i < lat_results->size(); i++)
    {
        latency_results[testid].emplace_back(lat_results->at(i));
    }
    clear_IOLat();
}

void test_SeqRead(int testid, StorageMod *storagemod, vector<char *> *workloadr_buf)
{
    cout << "test_SeqRead" << endl;
    pthread_t rtids[NUM_THREADS];
    uint64_t iosize = G_IOSIZE;
    tic(9);
    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        Worker_Info *info = (Worker_Info *)malloc(sizeof(Worker_Info));
        info->io_dir = false;
        info->is_rand = false;
        info->thread_id = i;
        info->io_size = iosize;
        // info->offset = o_align(i * (DATASET_SIZE / NUM_THREADS), iosize);
        info->offset = 0;
        info->count = (DATASET_SIZE / iosize / NUM_THREADS);
        info->loop = LOOP;
        info->storagemod = storagemod;
        info->is_degr_read = false;

        info->workload_buf = workloadr_buf->at(i);
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
    auto ret = print_throughtput(DATASET_SIZE * LOOP, LOOP * DATASET_SIZE / iosize, timer, "SEQ READ LOAD END");
    All_Write_Data.store(0);
    All_Read_Data.store(0);

    band_results.emplace_back(ret.first);
    iops_results.emplace_back(ret.second);

    vector<uint64_t> *lat_results = show_IOLat(NUM_THREADS);
    for (size_t i = 0; i < lat_results->size(); i++)
    {
        latency_results[testid].emplace_back(lat_results->at(i));
    }
    clear_IOLat();
}

void test_RandRead(int testid, StorageMod *storagemod, vector<char *> *workloadr_buf)
{
    cout << "test_RandRead" << endl;
    pthread_t rtids[NUM_THREADS];
    uint64_t iosize = G_IOSIZE;
    tic(9);
    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        Worker_Info *info = (Worker_Info *)malloc(sizeof(Worker_Info));
        info->io_dir = false; 
        info->is_rand = true;
        info->thread_id = i;
        info->io_size = iosize;
        info->offset = o_align(i * (DATASET_SIZE / NUM_THREADS), iosize);
        info->count = (DATASET_SIZE / iosize / NUM_THREADS);
        info->loop = LOOP;
        info->storagemod = storagemod;
        info->is_degr_read = false;

        info->workload_buf = workloadr_buf->at(i);
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
    auto ret = print_throughtput(DATASET_SIZE * LOOP, LOOP * DATASET_SIZE / iosize, timer, "RAND READ LOAD END");
    All_Write_Data.store(0);
    All_Read_Data.store(0);

    band_results.emplace_back(ret.first);
    iops_results.emplace_back(ret.second);

    vector<uint64_t> *lat_results = show_IOLat(NUM_THREADS);
    for (size_t i = 0; i < lat_results->size(); i++)
    {
        latency_results[testid].emplace_back(lat_results->at(i));
    }
    clear_IOLat();
}
