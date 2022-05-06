#ifndef ENCODE_H
#define ENCODE_H

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
#include <string.h>
#include <liburing.h>

// #include "threadpool11/threadpool11.hpp"
#include "concurrentqueue.h"

#include "metadata.h"
#include "logfiles.h"
#include "queues.h"
#include "define.h"
#include "ecEncoder.h"
#include "cache.h"

using namespace std;

class SEncodeMod
{
public:
    MetaMod *GloMeta;

public:
    const static int Max_ECthreads = NUM_WORKERS; 
    atomic_bool ecthreadbuf_flag[Max_ECthreads];  

    
    vector<ecEncoder *> v_ecEncoder;     
    vector<vector<char *>> v_olddatabuf; 
    vector<vector<char *>> v_newdatabuf; 
    vector<vector<char *>> v_temp_pbuf;  

    vector<vector<char *>> v_endatabuf;
    vector<vector<char *>> v_enparitybuf;

    int valid;           
    int check;           
    uint64_t chunk_size; 

public:
    V_DevFiles *v_stdfiles;

public:
    
    thread encode_t;

public:
    int bitmap_fd;
    char *bitmap_buf;

public:
    io_uring stdring[NUM_WORKERS];             
    atomic_uint64_t ring_pending[NUM_WORKERS]; 

public:
    SEncodeMod(int valid_in, int check_in, int chunksize, V_DevFiles *v_devfiles_in, MetaMod *Meta)
    {
        GloMeta = Meta;
        valid = valid_in;
        check = check_in;
        chunk_size = chunksize;
        v_stdfiles = v_devfiles_in;

        const int BufLen = 100;

        for (int i = 0; i < Max_ECthreads; i++)
        {
            ecthreadbuf_flag[i].store(false);

            vector<char *> data_buf;
            for (int n = 0; n < valid; n++)
            {
                char *ptr = NULL;
                int ret = posix_memalign((void **)&ptr, ALIGN_SIZE, SCHUNK_SIZE); 
                memset(ptr, 0x1a, SCHUNK_SIZE);
                data_buf.emplace_back(ptr);
            }
            v_endatabuf.emplace_back(data_buf);

            vector<char *> parity_buf;
            for (int n = 0; n < check; n++)
            {
                char *ptr = NULL;
                int ret = posix_memalign((void **)&ptr, ALIGN_SIZE, SCHUNK_SIZE); 
                memset(ptr, 0x1b, SCHUNK_SIZE);
                parity_buf.emplace_back(ptr);
            }
            v_enparitybuf.emplace_back(parity_buf);

            vector<char *> temp_buf;
            for (int n = 0; n < BufLen; n++)
            {
                char *ptr = NULL;
                int ret = posix_memalign((void **)&ptr, ALIGN_SIZE, SCHUNK_SIZE); 
                memset(ptr, 0x00, SCHUNK_SIZE);
                temp_buf.emplace_back(ptr);
            }
            v_temp_pbuf.emplace_back(temp_buf);

            vector<char *> olddata_buf;
            for (int n = 0; n < BufLen; n++)
            {
                char *ptr = NULL;
                int ret = posix_memalign((void **)&ptr, ALIGN_SIZE, SCHUNK_SIZE); 
                memset(ptr, 0x00, SCHUNK_SIZE);
                olddata_buf.emplace_back(ptr);
            }
            v_olddatabuf.emplace_back(olddata_buf);

            vector<char *> newdata_buf;
            for (int n = 0; n < BufLen; n++)
            {
                char *ptr = NULL;
                int ret = posix_memalign((void **)&ptr, ALIGN_SIZE, SCHUNK_SIZE);
                memset(ptr, 0x00, SCHUNK_SIZE);
                newdata_buf.emplace_back(ptr);
            }
            v_newdatabuf.emplace_back(newdata_buf);

            ecEncoder *encoder = new ecEncoder(valid, check, chunk_size);
            v_ecEncoder.emplace_back(encoder);
        }

        for (size_t i = 0; i < Max_ECthreads; i++)
        {
            io_uring_queue_init(RING_QD, &stdring[i], RING_FLAG);
        }

        bitmap_fd = open(v_stdfiles->at(0)->file_path.c_str(), O_RDWR | O_DIRECT);
        assert(bitmap_fd != -1);
        int ret = posix_memalign((void **)&bitmap_buf, ALIGN_SIZE, 4096);
        assert(ret == 0);
    };

    ~SEncodeMod(){};

    bool encode_fullstripe(int thread, vector<DIO_Info> v_dios);
    bool encode_partialstripe(int thread, vector<DIO_Info> v_dios);
};


#endif