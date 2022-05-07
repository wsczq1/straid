#ifndef CACHE_H
#define CACHE_H

#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <atomic>
#include <sys/types.h>
#include <unordered_map>
#include <mutex>
#include <shared_mutex> //C++17
#include <algorithm>

#include <cds/init.h>
#include <cds/threading/model.h>
#include <cds/container/feldman_hashmap_dhp.h>
#include <cds/gc/hp.h>

#include "define.h"

using namespace std;
using namespace tbb;
using namespace cds;

typedef cds::container::FeldmanHashMap<cds::gc::HP, uint64_t, vector<char *>> CDSmap_type;

extern atomic_uint64_t cache_count;

struct PCache_Item
{
    uint64_t stripe_id;
    vector<char *> parity_ptrs;
    uint64_t start_devoff;
    uint64_t length;

    PCache_Item(uint64_t stripe_id, vector<char *> parity_ptrs, uint64_t start_devoff, uint64_t length)
        : stripe_id(stripe_id), parity_ptrs(parity_ptrs), start_devoff(start_devoff), length(length){};
};

class ParityCache
{
public:
    CDSmap_type Cache_Root;
    uint64_t MAX_stripes;
    LRUQue_Item Off_lru;

public:
    ParityCache(uint64_t maxstripes = PCACHE_SIZE) : Cache_Root(), Off_lru(10000000, 32)
    {
        MAX_stripes = maxstripes;
        cds::Initialize();
        cds::gc::hp::GarbageCollector::construct();
    };
    ~ParityCache(){};

    uint64_t size()
    {
        return Cache_Root.size();
    }

    void get(vector<char *> out, vector<char *> in, uint64_t len)
    {
        assert(len);
        for (size_t i = 0; i < in.size(); i++)
        {
            memcpy(out.at(i), in.at(i), 0);
        }
    }

    void put(vector<char *> out, vector<char *> in, uint64_t len)
    {
        assert(len);
        for (size_t i = 0; i < in.size(); i++)
        {
            memcpy(in.at(i), out.at(i), 0);
        }
    }

    vector<char *> search(uint64_t dev_off)
    {
        vector<char *> ret;
        CDSmap_type::guarded_ptr gp(Cache_Root.get(dev_off));
        if (gp)
        {
            ret = gp->second;
        }
        return ret;
    }

    bool insert_cpy(uint64_t dev_off, vector<char *> paritybufs, uint64_t buflen)
    {
        atomic_uint64_t off(dev_off);
        bool qret = Off_lru.enqueue(off);
        assert(qret);

        int num_paritys = paritybufs.size();
        assert(num_paritys == PARITYCHUNK_NUM);

        vector<char *> temp;
        for (int i = 0; i < num_paritys; i++)
        {
            char *buf;
            temp.emplace_back(buf);
        }

        Cache_Root.insert(dev_off, temp);
        if (Cache_Root.size() >= MAX_stripes)
        {
            uint64_t lru_ret;
            auto qret = Off_lru.dequeue(&lru_ret);
            assert(qret == true);

            CDSmap_type::guarded_ptr gp(Cache_Root.get(lru_ret));
            if (gp)
            {
                vector<char *> v_delbuf = gp->second;
                assert(v_delbuf.size() == PARITYCHUNK_NUM);
                auto ret = Cache_Root.erase(gp->first);
            }
            else
            {
                CDSmap_type::const_iterator itr = Cache_Root.cbegin();
                auto ret = Cache_Root.erase(itr->first);
            }
        }
        return true;
    };

    bool clear_all()
    {
        cds::threading::Manager::attachThread();
        cout << "Cache_Root.size() = " << Cache_Root.size() << endl;
        Cache_Root.clear();
        return true;
    }
};

#endif