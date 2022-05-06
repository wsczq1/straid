#ifndef QUEUES_H
#define QUEUES_H

#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <sys/types.h>
#include <atomic>

#include "metadata.h"
#include "define.h"
#include "concurrentqueue.h"

using namespace std;

bool is_all_queue_empty(V_DevQue *v_logques);

struct BatchQueue
{
    int Worker_ID;          
    DevQue_Item batch_queue; 
    atomic_uint64_t inqueue_data_len;

    BatchQueue(int workerid) : batch_queue(KB), inqueue_data_len(0)
    {
        Worker_ID = workerid;
    }
    ~BatchQueue()
    {
    }
};

struct UserQueue
{
    int User_ID; 

    UsrQue_Item uio_queue; 
    atomic_uint64_t inqueue_data_len;

    UserQueue(int userid) : uio_queue(QUE_MAXLEN), inqueue_data_len(0)
    {
        User_ID = userid;
    }
    ~UserQueue()
    {
    }

    bool operator<(const UserQueue &LB) const
    {
        return User_ID < LB.User_ID;
    };
    bool operator>(const UserQueue &LB) const
    {
        return User_ID > LB.User_ID;
    };
};


struct DevQueue
{
    int Dev_ID;         
    uint64_t buf_maxlen; 

    DevQue_Item *dev_queue; 
    atomic_uint64_t inqueue_data_len;

    DevQueue(int fileid)
    {
        Dev_ID = fileid;
        dev_queue = new DevQue_Item(QUE_MAXLEN);
        inqueue_data_len.store(0);
    }
    ~DevQueue()
    {
        delete dev_queue;
    }

    bool operator<(const DevQueue &LB) const 
    {
        return Dev_ID < LB.Dev_ID;
    };
    bool operator>(const DevQueue &LB) const
    {
        return Dev_ID > LB.Dev_ID;
    };
};

#endif