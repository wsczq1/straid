#ifndef LOGFILES_H
#define LOGFILES_H

#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <assert.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/types.h>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <liburing.h>

#include "define.h"

using namespace std;

uint64_t user2dev(uint64_t user_offset, int *devid, int *stripeid); 
bool is_fullstripe(uint64_t useroffset, uint64_t length);
uint64_t user2stripe(uint64_t user_offset);
int logical_distance(int src, int dest, int loop);
int devoff2stripe(uint64_t devoff);
int stripe2datapos(int stripeid);
int stripe2paritypos(int stripeid);
uint64_t stripe2useroff(int stripeid);
uint64_t stripe2devoff(int stripeid, uint64_t base_off);

struct DevFile
{
    int Dev_ID;            
    string file_path;      
    int file_fd;           
    uint64_t start_offset; 
    uint64_t end_offset;   

    atomic_uint64_t curr_offset; 

    DevFile(int devid)
    {
        Dev_ID = devid;
        start_offset = 0;
        end_offset = 0;
        curr_offset.store(0);
    };

    DevFile(int devid, string file_path, int file_fd, uint64_t start_offset, uint64_t end_offset)
        : Dev_ID(devid), file_path(file_path), file_fd(file_fd), start_offset(start_offset), end_offset(end_offset)
    {
        curr_offset.store(start_offset);
    };

    ~DevFile()
    {
    }

    bool operator<(const DevFile &LF) const 
    {
        return Dev_ID < LF.Dev_ID;
    };
    bool operator>(const DevFile &LF) const 
    {
        return Dev_ID > LF.Dev_ID;
    };
};

#endif