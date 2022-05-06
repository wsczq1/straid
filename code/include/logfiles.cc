#ifndef LOGFILES_CC
#define LOGFILES_CC

#include "logfiles.h"


uint64_t user2stripe(uint64_t user_offset)
{
    return user_offset / SCHUNK_SIZE / DATACHUNK_NUM;
}


bool is_fullstripe(uint64_t useroffset, uint64_t length)
{
    if (useroffset % SSTRIPE_DATASIZE != 0 || length != SSTRIPE_DATASIZE)
    {
        return false;
    }
    return true;
}


uint64_t user2dev(uint64_t user_offset, int *devid, int *stripeid)
{
    uint64_t converted_offset = 0;

    uint64_t chunk = user_offset / SCHUNK_SIZE;         
    uint64_t remain_offset = user_offset % SCHUNK_SIZE; 

    int stripe_num = chunk / (DATACHUNK_NUM); 
    int remain_chunk = chunk % (DATACHUNK_NUM);
    if (stripeid != NULL)
    {
        *stripeid = stripe_num;
    }

    
    int dev_pos = stripe_num % NUM_DEVFILES + remain_chunk;
    if (dev_pos >= NUM_DEVFILES)
    {
        dev_pos -= NUM_DEVFILES;
    }
    if (devid != NULL)
    {
        *devid = dev_pos;
    }
    // cout << "user2dev: " << user_offset << " " << stripe_num << " " << chunk << " " << dev_pos << endl;

    
    
    converted_offset = stripe_num * SCHUNK_SIZE + remain_offset;

    return converted_offset;
}


int logical_distance(int src, int dest, int loop)
{
    if (src <= dest)
    {
        return (dest - src);
    }
    else
    {
        return (dest - src + loop);
    }
    return -1;
}

int devoff2stripe(uint64_t devoff)
{
    return devoff / SCHUNK_SIZE;
}


int stripe2datapos(int stripeid)
{
    return stripeid % NUM_DEVFILES;
}


int stripe2paritypos(int stripeid)
{
    int datapos = stripe2datapos(stripeid);
    datapos += DATACHUNK_NUM;
    if (datapos >= NUM_DEVFILES)
    {
        datapos -= NUM_DEVFILES;
    }

    return datapos;
}


uint64_t stripe2useroff(int stripeid)
{
    return stripeid * SSTRIPE_DATASIZE;
}


uint64_t stripe2devoff(int stripeid, uint64_t base_off)
{
    return base_off + stripeid * SCHUNK_SIZE;
}

#endif