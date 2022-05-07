#ifndef DECODE_H
#define DECODE_H

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

#include "logfiles.h"
#include "queues.h"
#include "concurrentqueue.h"
#include "define.h"
#include "ecEncoder.h"
#include "metadata.h"

using namespace std;

class SDecodeMod
{
public:
    MetaMod *GloMeta;

public:
    const static int Max_ECthreads = NUM_WORKERS; 
    atomic_bool ecthreadbuf_flag[Max_ECthreads];  

    
    vector<ecEncoder *> v_ecDecoder; 
    vector<vector<char *>> v_dedatabuf;
    vector<vector<char *>> v_deparitybuf;

    int valid;         
    int check;         
    size_t chunk_size; 

public:
    V_DevFiles *v_stdfiles;

public:
    io_uring stdring[NUM_WORKERS];             
    atomic_uint64_t ring_pending[NUM_WORKERS]; 

public:
    
    SDecodeMod(int valid_in, int check_in, size_t chunksize, V_DevFiles *v_logfiles_in, MetaMod *Meta)
    {
        
        GloMeta = Meta;
        valid = valid_in;
        check = check_in;
        chunk_size = chunksize;
        v_stdfiles = v_logfiles_in;

        
        vector<char *> data_buf;
        for (int nlog = 0; nlog < valid; nlog++)
        {
            char *ptr = NULL;
            int ret = posix_memalign((void **)&ptr, ALIGN_SIZE, SCHUNK_SIZE);
            memset(ptr, 0x03, SCHUNK_SIZE);
            data_buf.emplace_back(ptr);
        }
        v_dedatabuf.emplace_back(data_buf);

        vector<char *> parity_buf;
        for (int nlog = 0; nlog < check; nlog++)
        {
            char *ptr = NULL;
            int ret = posix_memalign((void **)&ptr, ALIGN_SIZE, SCHUNK_SIZE);
            memset(ptr, 0x04, SCHUNK_SIZE);
            parity_buf.emplace_back(ptr);
        }
        v_deparitybuf.emplace_back(parity_buf);

        
        ecEncoder *decoder = new ecEncoder(valid, check, chunk_size);
        v_ecDecoder.emplace_back(decoder);

        
        for (size_t i = 0; i < NUM_WORKERS; i++)
        {
            io_uring_queue_init(RING_QD, &stdring[i], RING_FLAG);
        }
    };

    ~SDecodeMod()
    {
    }

    uint64_t s_norRead(int thread_id, vector<DIO_Info> v_uios);
    uint64_t s_degRead(int thread_id, vector<char *> *vdatabuf, vector<uint64_t> *vdataoff, vector<int> *vdatapos, vector<uint64_t> *vdatalen);
};

#endif