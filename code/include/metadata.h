#ifndef METADATA_H
#define METADATA_H

#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unordered_map>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <math.h>
#include <shared_mutex> //C++17
#include <algorithm>
#include <bitset> // std::bitset

#include "define.h"
#include "ecEncoder.h"
#include "logfiles.h"
#include "cache.h"
#include "concurrentqueue.h"
#include <atomic_bitvector.hpp>

using namespace std;

/**
 * Device IO Info
 * */
struct DIO_Info
{
    int dev_id;
    char *buf;
    uint64_t dev_offset;
    uint64_t length;

    DIO_Info()
        : dev_id(-1), buf(NULL), dev_offset(0), length(0){};

    DIO_Info(int devid, char *ptr, uint64_t offset, uint64_t length)
        : dev_id(devid), buf(ptr), dev_offset(offset), length(length){};
};

/**
 * User IO Info
 * */
struct UIO_Info
{
    int user_id;

    bool is_write; // IO direction
    char *buf;
    uint64_t user_offset;
    uint64_t length;

    UIO_Info()
        : user_id(-1), buf(NULL), user_offset(0), length(0){};

    UIO_Info(int userid, bool is_write, char *ptr, uint64_t offset, uint64_t length)
        : user_id(userid), is_write(is_write), buf(ptr), user_offset(offset), length(length){};

    DIO_Info s_uio2dio()
    {
        int devid = 0;
        uint64_t dev_off = user2dev(user_offset, &devid, NULL);
        DIO_Info dio(devid, buf, dev_off, length);
        assert(dio.dev_id < NUM_DEV);
        assert(dio.buf != NULL);
        assert(dio.dev_offset >= 0 && dio.dev_offset < USER_SPACE_LEN);
        assert(dio.length >= 0);
        return dio;
    };
};


struct StdStripe
{
    int Stripe_ID;
    uint64_t chunk_size;

    StdStripe(int stripeid)
    {
        Stripe_ID = stripeid;
        chunk_size = SCHUNK_SIZE;
    };
};

struct SSTEntry
{
    uint64_t SSTstripeid;
    atomic_uint64_t SSTthreadid;
    atomic_bool stripe_lock;
    atomic_bool is_frozen;

    SSTEntry() : SSTthreadid(INT16_MAX), stripe_lock(false), is_frozen(false)
    {
        SSTstripeid = 0;
    }

    SSTEntry(int stripeid, int threadid) : SSTthreadid(threadid), stripe_lock(false), is_frozen(false)
    {
        SSTstripeid = stripeid;
    }
};

struct SSTable
{
    vector<SSTEntry *> tablehash;

    SSTable()
    {
        for (size_t i = 0; i <= (USER_SPACE_LEN / SSTRIPE_DATASIZE) + 100; i++)
        {
            SSTEntry *sse = new SSTEntry(i, INT16_MAX);
            tablehash.emplace_back(sse);
        }
    }
    ~SSTable()
    {
    }

    bool search_SST(uint64_t stripeid, SSTEntry *&entry)
    {
        assert(stripeid < tablehash.size());
        // if (stripeid >= tablehash.size())
        // {
        //     cout << stripeid << " " << tablehash.size() << endl;
        // }
        entry = tablehash.at(stripeid);
        return true;
    }

    bool update_SST(uint64_t stripeid, uint64_t threadid)
    {
        assert(stripeid < tablehash.size());
        SSTEntry *entry = tablehash.at(stripeid);
        // assert(entry->stripe_lock.load() == true);
        entry->is_frozen.store(false);
        entry->SSTthreadid.store(threadid);
        return true;
    }

    bool delete_SST(uint64_t stripeid)
    {
        assert(stripeid < tablehash.size());
        SSTEntry *entry = tablehash.at(stripeid);

        entry->SSTthreadid.store(INT16_MAX);
        entry->is_frozen.store(true);
        entry->stripe_lock.store(false);

        return true;
    }
};

struct SShashtable
{
    Table_Hash tablehash;

    SShashtable() : tablehash()
    {
        tablehash.reserve(512);
    }
    ~SShashtable()
    {
        tablehash.clear();
    }

    // bool search_SST(uint64_t stripeid, SSTEntry *&entry)
    // {

    //     if (tablehash.find(stripeid, entry))
    //     {
    //         return true;
    //     }
    //     else
    //     {
    //         return false;
    //     }
    // }

    // bool searchget_SST(uint64_t stripeid, uint64_t threadid, SSTEntry *&entry)
    // {
    //     bool handled = false;
    //     if (tablehash.find(stripeid, entry))
    //     {
    //         assert(entry != NULL);
    //         return true;
    //     }
    //     else
    //     {
    //         SSTEntry *entry = new SSTEntry(stripeid, threadid);
    //         bool ret = tablehash.insert_or_assign(stripeid, entry);
    //         assert(entry != NULL);
    //         return false;
    //     }
    // }

    // bool delete_SST(uint64_t stripeid)
    // {
    //     SSTEntry *entry = NULL;
    //     if (tablehash.find(stripeid, entry))
    //     {
    //         entry->SSTstripeid = INT32_MAX;
    //         entry->SSTthreadid = INT16_MAX;
    //         entry->is_frozen.store(true);
    //         entry->stripe_lock.store(false);

    //         bool ret = tablehash.erase(stripeid);
    //         assert(ret == true);
    //         return true;
    //     }
    //     else
    //     {
    //         return false;
    //     }
    // }
};

// RAID Meta Module
class StdRAID_Meta
{
public:
    int data_chunk_num;   // Data chunk num in a stripe
    int parity_chunk_num; // Parity chunk num in a stripe
    int chunks_num;
    int chunk_size; // chunk size

    vector<StdStripe *> stripemeta;
    V_DevFiles *v_stdfiles; // block storages
    vector<uint64_t> Disk_Start_offset;
    vector<uint64_t> Disk_Max_offset;

    ParityCache cache_mod; // ParityCache
    SSTable sstable_mod;   // SStable

public:
    StdRAID_Meta(V_DevFiles *v_stdfiles)
        : v_stdfiles(v_stdfiles), cache_mod(PCACHE_SIZE), sstable_mod()
    {
        data_chunk_num = DATACHUNK_NUM;
        parity_chunk_num = PARITYCHUNK_NUM;
        chunks_num = data_chunk_num + parity_chunk_num;
        chunk_size = SCHUNK_SIZE;

        for (size_t i = 0; i < v_stdfiles->size(); i++)
        {
            Disk_Start_offset.emplace_back(v_stdfiles->at(i)->start_offset);
            Disk_Max_offset.emplace_back(v_stdfiles->at(i)->end_offset);
        }

        uint64_t max_dev_off = Disk_Max_offset[0] - Disk_Start_offset[0];
        int chunk = max_dev_off / SCHUNK_SIZE + 1;
        int num_stripes = chunk / (DATACHUNK_NUM) + 1;
        for (int i = 0; i <= num_stripes; i++)
        {
            StdStripe *stmeta = new StdStripe(i);
            stripemeta.emplace_back(stmeta);
        }
    }
    ~StdRAID_Meta(){

    };
};

class BLK_Bitmap
{
public:
    atomicbitvector::atomic_bv_t *bitmap;

    uint64_t store_mapsize;
    uint64_t store_offset;
    string store_dev_path;
    int store_dev_fd;

    uint64_t Part_bitnum;
    uint64_t Part_cnt;

    atomic_uint64_t v_updated_cnt[MB];

    timespec last_flushtime[MB];
    timespec now_time[MB];
    int Flush_TH;

    thread bm_persist_t;
    io_uring Bitmap_Ring;

public:
    BLK_Bitmap(uint64_t max_bitnum, uint64_t Factor) : v_updated_cnt(), Flush_TH(100 * 1000)
    {
        Part_bitnum = Factor;
        Part_cnt = max_bitnum / Part_bitnum + 1;
        bitmap = new atomicbitvector::atomic_bv_t(max_bitnum);

        for (size_t i = 0; i <= Part_cnt; i++)
        {
            v_updated_cnt[i].store(0);
            comm_tic(&last_flushtime[i]);
        }

        io_uring_queue_init(RING_QD, &Bitmap_Ring, RING_FLAG);
        // bm_persist_t = thread(&BLK_Bitmap::bitmap_run, this);
    };
    ~BLK_Bitmap()
    {
        if (bm_persist_t.joinable())
        {
            bm_persist_t.join();
        }
    };

    void set(size_t idx, bool value)
    {
        if (idx >= bitmap->size())
            return;
        bitmap->set(idx, value);
        v_updated_cnt[idx / Part_bitnum].fetch_add(1);
    }

    void reset(size_t idx)
    {
        if (idx >= bitmap->size())
            return;
        bitmap->reset(idx);
        v_updated_cnt[idx / Part_bitnum].fetch_add(1);
    }

    void flush_all()
    {
        pwrite64(store_dev_fd, (char *)&bitmap->data_, sizeof(bitmap->data_), store_offset);
    }

    void bitmap_run()
    {
        cout << "Bitmap Flush Thread Run" << endl;
        while (1)
        {
            int flushed = 0;
            for (size_t part = 0; part < Part_cnt; part++)
            {
                uint64_t time = comm_toc(&last_flushtime[part], &now_time[part]);

                if (v_updated_cnt[part].load() > 0 && time >= Flush_TH)
                {
                    uint64_t off = part * Part_bitnum * sizeof(bitmap->data_.at(0));
                    char *buf = (char *)&bitmap->data_ + off;
                    uint64_t offset = store_offset + off;
                    uint64_t length = Part_bitnum * sizeof(bitmap->data_.at(0));
                    iouring_wprep(&Bitmap_Ring, store_dev_fd, buf, offset, length);

                    flushed++;
                    v_updated_cnt[part].store(0);
                    comm_tic(&last_flushtime[part]);
                }
            }
            int wait_cnt = flushed >= RING_QD ? RING_QD : flushed;
            if (wait_cnt)
            {
                uint64_t ret = io_uring_submit(&Bitmap_Ring);
                iouring_wait(&Bitmap_Ring, wait_cnt);
            }
        }
    };
};

// RAID system Meta Module
class MetaMod
{
public:
    // space information
    uint64_t User_base_offset;
    uint64_t User_max_offset;

    uint64_t Std_raid_space;
    StdRAID_Meta *StdRAID_meta;

public:
    BLK_Bitmap *blk_bitmap;

public:
    MetaMod(uint64_t userbase, uint64_t usermax, V_DevFiles *v_stdfiles)
    {
        StdRAID_meta = new StdRAID_Meta(v_stdfiles);

        blk_bitmap = new BLK_Bitmap(ceil((double)(usermax - userbase) / BLK_SIZE), 1024);
        blk_bitmap->store_dev_path = v_stdfiles->at(0)->file_path;
        blk_bitmap->store_dev_fd = open(blk_bitmap->store_dev_path.c_str(), O_RDWR | O_DIRECT);
        blk_bitmap->store_offset = STRA_SPACE_LEN;
    }

    ~MetaMod(){

    };
};

#endif