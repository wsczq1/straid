#ifndef ENCODE_CC
#define ENCODE_CC

#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <memory.h>

#include "metadata.h"
#include "encode.h"
#include "storageMod.h"
#include "logfiles.h"

#define PARITY_CACHE

extern atomic_uint64_t incremental_stripeid;
extern atomic_bool proc_end_flag;
extern atomic_uint64_t Batch_Count;
extern atomic_uint64_t Cache_Hit;
extern atomic_uint64_t Cache_Miss;
StorageMod *GloStor;

bool SEncodeMod::encode_fullstripe(int tid, vector<DIO_Info> v_dios)
{
    assert(v_dios.size() == DATACHUNK_NUM);
    auto batchque = &GloStor->v_batchque.at(tid)->batch_queue;
    auto batchque_len = &GloStor->v_batchque.at(tid)->inqueue_data_len;
    auto SST = &GloStor->meta_mod->StdRAID_meta->sstable_mod;
    int stripeid = devoff2stripe(v_dios[0].dev_offset);

    vector<char *> v_databuf;
    for (size_t i = 0; i < DATACHUNK_NUM; i++)
    {
        memcpy(v_endatabuf[tid].at(i), v_dios.at(i).buf, v_dios.at(i).length);
    }

    SSTEntry *ssentry;
    bool sret = SST->search_SST(stripeid, ssentry);
    assert(sret == true);
    ssentry->is_frozen.store(true);

    ecEncoder *ecencoder = v_ecEncoder[tid];
    ecencoder->do_full_encode(v_endatabuf[tid], v_enparitybuf[tid]);

    for (size_t i = 0; i < DATACHUNK_NUM; i++)
    {
        DIO_Info dio = v_dios.at(i);
        DevFile *destdev = GloMeta->StdRAID_meta->v_stdfiles->at(dio.dev_id);
        int fd = destdev->file_fd;
        uint64_t devoff = dio.dev_offset;
        iouring_wprep(&stdring[tid], fd, v_endatabuf[tid].at(i), devoff, chunk_size);
        // printf("Encoder: fd:%d  datapos:%d  devoff:%ld\n", fd, vdatapos->at(i), devoff);
        // printf("Encoder: v_endatabuf:%x\n", (int)v_endatabuf[tid].at(i)[0]);
    }
    for (size_t i = 0; i < PARITYCHUNK_NUM; i++)
    {
        int ppos = stripe2paritypos(stripeid) + i;
        if (ppos >= NUM_DEVFILES)
        {
            ppos -= NUM_DEVFILES;
        }
        DevFile *destdev = GloMeta->StdRAID_meta->v_stdfiles->at(ppos);
        int fd = destdev->file_fd;
        uint64_t devoff = v_dios.at(0).dev_offset;
        iouring_wprep(&stdring[tid], fd, v_enparitybuf[tid].at(i), devoff, chunk_size);
        // printf("Encoder: fd:%d  paritypos:%d  devoff:%ld\n", fd, ppos, devoff);
        // printf("Encoder: v_enparitybuf:%x\n", (int)v_enparitybuf[tid].at(i)[0]);
    }
    uint64_t ret = io_uring_submit(&stdring[tid]);
    iouring_wait(&stdring[tid], ret);

    return true;
}

bool SEncodeMod::encode_partialstripe(int tid, vector<DIO_Info> v_dios)
{
    assert(v_dios.size() > 0);

    uint64_t pchunk_soff = INT_MAX;
    uint64_t pchunk_slen = 0;

    auto paritycache = &GloMeta->StdRAID_meta->cache_mod;
    auto queue = &GloStor->v_batchque2.at(tid);
    auto batchque_len = &GloStor->v_batchque.at(tid)->inqueue_data_len;
    auto SST = &GloStor->meta_mod->StdRAID_meta->sstable_mod;

    int stripeid = devoff2stripe(v_dios[0].dev_offset);
    uint64_t start_devoff = stripe2devoff(stripeid, 0);

    uint64_t vios_len = 0;
    for (size_t i = 0; i < v_dios.size(); i++)
    {
        pchunk_soff = min(pchunk_soff, v_dios.at(i).dev_offset);
        pchunk_slen = max(pchunk_slen, v_dios.at(i).length);
        vios_len += v_dios[i].length;
    }

    vector<char *> stemp_ptrs = paritycache->search(pchunk_soff);
    if (stemp_ptrs.size() == PARITYCHUNK_NUM)
    {
        Cache_Hit.fetch_add(1);
        paritycache->get(v_enparitybuf.at(tid), stemp_ptrs, pchunk_slen);
    }
    else
    {
        Cache_Miss.fetch_add(1);
        for (size_t i = 0; i < PARITYCHUNK_NUM; i++)
        {
            char *readbuf = v_enparitybuf.at(tid).at(i);
            uint64_t roffset = start_devoff;
            uint64_t rlen = pchunk_slen;
            DevFile *destdev = v_stdfiles->at(stripe2paritypos(stripeid));
            int fd = destdev->file_fd;
            iouring_rprep(&stdring[tid], fd, readbuf, roffset, rlen);
            // printf("Parity Read: datapos:%d  devoff:%ld, len:%ld\n", stripe2paritypos(stripeid), roffset, rlen);
        }
    }

    for (size_t i = 0; i < v_dios.size(); i++)
    {
        char *readbuf = v_olddatabuf.at(tid).at(i);
        uint64_t roffset = v_dios[i].dev_offset;
        uint64_t rlen = v_dios[i].length;
        DevFile *destdev = v_stdfiles->at(v_dios[i].dev_id);
        int fd = destdev->file_fd;
        iouring_rprep(&stdring[tid], fd, readbuf, roffset, rlen);
    }
    uint64_t rret = io_uring_submit(&stdring[tid]);
    iouring_wait(&stdring[tid], rret);

    // Frozen
    SSTEntry *ssentry;
    SST->search_SST(stripeid, ssentry);
    ssentry->is_frozen.store(true);

    vector<DIO_Info> bat_dios;
    uint qlen = batchque_len->load();
    if (qlen > 0)
    {
        // cout << " bat_dios " << qlen << endl;
        bat_dios.assign(queue->begin(), queue->begin() + qlen);
        Batch_Count.fetch_add(qlen);
        batchque_len->store(0);
    }

    for (size_t i = 0; i < bat_dios.size(); i++)
    {
        pchunk_soff = min(pchunk_soff, bat_dios.at(i).dev_offset);
        pchunk_slen = max(pchunk_slen, bat_dios.at(i).length);
    }

    uint64_t batch_iolen = 0;
    vector<uint64_t> v_chunkoff;
    vector<uint64_t> v_length;
    for (size_t i = 0; i < v_dios.size(); i++)
    {
        v_chunkoff.emplace_back(v_dios[i].dev_offset - start_devoff);
        v_length.emplace_back(v_dios[i].length);
    }
    for (size_t i = 0; i < bat_dios.size(); i++)
    {
        if (bat_dios[i].dev_offset < start_devoff)
        {
            continue;
        }
        v_chunkoff.emplace_back(bat_dios[i].dev_offset - start_devoff);
        v_length.emplace_back(bat_dios[i].length);
        batch_iolen += bat_dios[i].length;
    }

    if (batch_iolen > ((SCHUNK_SIZE * DATACHUNK_NUM - vios_len) >> 1))
    {
        vector<DIO_Info> re_dios;
        vector<int> index;
        for (size_t i = 0; i < DATACHUNK_NUM; i++)
        {
            index.emplace_back(i);
        }
        for (size_t i = 0; i < bat_dios.size(); i++)
        {
            int val = bat_dios.at(i).dev_id;
            index.erase(remove(index.begin(), index.end(), val), index.end());
        }
        for (size_t i = 0; i < index.size(); i++)
        {
            DIO_Info dio(index[i], v_endatabuf[tid].at(index[i]), stripe2devoff(stripeid, 0), SCHUNK_SIZE);
            re_dios.emplace_back(dio);
        }

        for (size_t i = 0; i < re_dios.size(); i++)
        {
            char *readbuf = re_dios.at(i).buf;
            uint64_t roffset = re_dios[i].dev_offset;
            uint64_t rlen = re_dios[i].length;
            DevFile *destdev = v_stdfiles->at(re_dios[i].dev_id);
            int fd = destdev->file_fd;
            iouring_rprep(&stdring[tid], fd, readbuf, roffset, rlen);
        }
        rret = io_uring_submit(&stdring[tid]);
        iouring_wait(&stdring[tid], rret);

        ecEncoder *ecencoder = v_ecEncoder[tid];
        ecencoder->do_full_encode(v_endatabuf[tid], v_enparitybuf[tid]);
    }
    else
    {
        for (size_t i = 0; i < bat_dios.size(); i++)
        {
            char *readbuf = v_olddatabuf.at(tid).at(i + v_dios.size());
            uint64_t roffset = bat_dios[i].dev_offset;
            uint64_t rlen = bat_dios[i].length;
            DevFile *destdev = v_stdfiles->at(bat_dios[i].dev_id);
            int fd = destdev->file_fd;
            iouring_rprep(&stdring[tid], fd, readbuf, roffset, rlen);
        }
        rret = io_uring_submit(&stdring[tid]);
        iouring_wait(&stdring[tid], rret);

        ecEncoder *ecencoder = v_ecEncoder[tid];
        ecencoder->do_part_encode(v_olddatabuf[tid], v_newdatabuf[tid], v_enparitybuf[tid], v_chunkoff, v_length);
    }

    for (size_t dchunk = 0; dchunk < v_dios.size(); dchunk++)
    {
        DevFile *destdev = GloMeta->StdRAID_meta->v_stdfiles->at(v_dios.at(dchunk).dev_id);
        int fd = destdev->file_fd;
        uint64_t woffset = v_dios.at(dchunk).dev_offset;
        uint64_t wlen = v_dios.at(dchunk).length;
        iouring_wprep(&stdring[tid], fd, v_dios.at(dchunk).buf, woffset, wlen);
        // printf("Encoder: thischunk:%ld, datapos:%d  devoff:%ld, len:%ld\n", thischunk, vdatapos->at(thischunk), woffset, wlen);
        // printf("Encoder: v_endatabuf:%x\n", (int)v_endatabuf[tid].at(thischunk)[0]);
    }
    for (size_t dchunk = 0; dchunk < bat_dios.size(); dchunk++)
    {
        DevFile *destdev = GloMeta->StdRAID_meta->v_stdfiles->at(bat_dios.at(dchunk).dev_id);
        int fd = destdev->file_fd;
        uint64_t woffset = bat_dios.at(dchunk).dev_offset;
        uint64_t wlen = bat_dios.at(dchunk).length;
        iouring_wprep(&stdring[tid], fd, bat_dios.at(dchunk).buf, woffset, wlen);
        // printf("Encoder: thischunk:%ld, datapos:%d  devoff:%ld, len:%ld\n", thischunk, vdatapos->at(thischunk), woffset, wlen);
        // printf("Encoder: v_endatabuf:%x\n", (int)v_endatabuf[tid].at(thischunk)[0]);
    }

    for (size_t pchunk = 0; pchunk < PARITYCHUNK_NUM; pchunk++)
    {
        int ppos = stripe2paritypos(stripeid);
        if (ppos >= NUM_DEVFILES)
        {
            ppos -= NUM_DEVFILES;
        }
        DevFile *destdev = GloMeta->StdRAID_meta->v_stdfiles->at(ppos);
        int fd = destdev->file_fd;
        uint64_t woffset = pchunk_soff;
        uint64_t wlen = pchunk_slen;
        iouring_wprep(&stdring[tid], fd, v_enparitybuf[tid].at(pchunk), woffset, wlen);
        // printf("Encoder: paritypos:%d  devoff:%ld, len:%ld\n", ppos, woffset, wlen);
        // printf("Encoder: v_enparitybuf:%x\n", (int)v_enparitybuf[tid].at(pchunk)[0]);
    }
    uint64_t wwret = io_uring_submit(&stdring[tid]);

    vector<char *> temp_ptrs = paritycache->search(pchunk_soff);
    if (temp_ptrs.size() == PARITYCHUNK_NUM)
    {
        for (size_t p = 0; p < PARITYCHUNK_NUM; p++)
        {
            paritycache->get(v_enparitybuf.at(tid), stemp_ptrs, pchunk_slen);
        }
    }
    else
    {
        vector<char *> temp;
        for (size_t p = 0; p < PARITYCHUNK_NUM; p++)
        {
            temp.emplace_back(v_enparitybuf[tid].at(p));
        }
        paritycache->insert_cpy(pchunk_soff, temp, BLK_SIZE);
    }
    iouring_wait(&stdring[tid], wwret);

    return true;
}

#endif