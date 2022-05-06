#ifndef STORAGEMOD_CC
#define STORAGEMOD_CC

#include <math.h>
#include "storageMod.h"
#include "encode.h"

extern atomic_uint64_t ring_pending[NUM_THREADS];
extern atomic_uint64_t Block_Count;

void Func_init(int fd, uint64_t start_offset, uint64_t end_offset, char *buf)
{
    for (size_t i = start_offset; i < end_offset; i += MB)
    {
        uint64_t ret = pwrite64(fd, buf, (MB), i);
        assert(ret);
    }
}
bool StorageMod::Init_Raid(V_DevFiles *v_devfiles)
{
    cout << "START Initing RAID" << endl;

    char *buf;
    int ret = posix_memalign((void **)&buf, ALIGN_SIZE, (MB));
    assert(ret == 0);
    memset(buf, 0x0, (MB));

    thread tid[v_devfiles->size()];

    
    for (size_t i = 0; i < v_devfiles->size(); i++)
    {
        int fd = v_devfiles->at(i)->file_fd;
        uint64_t start_offset = v_devfiles->at(i)->start_offset;
        uint64_t end_offset = start_offset + DATASET_SIZE;

        tid[i] = thread(&Func_init, fd, start_offset, end_offset, buf);
    }
    for (size_t i = 0; i < v_devfiles->size(); i++)
    {
        tid[i].join();
    }

    cout << "End Initing RAID" << endl;
    return true;
}

void StorageMod::split_stripe(uint64_t offset, uint64_t length, vector<int> *stripe_id, vector<uint64_t> *stripe_soff, vector<uint64_t> *stripe_len)
{
    int start_stripeid = 0;
    int end_stripeid = 0;
    user2dev(offset, NULL, &start_stripeid);
    user2dev(offset + length - 1, NULL, &end_stripeid);

    uint64_t start_off = offset;
    uint64_t remain_len = length;

    for (int i = start_stripeid; i <= end_stripeid; i++)
    {
        uint64_t this_stripe_eoff = stripe2useroff(i) + SSTRIPE_DATASIZE;

        stripe_soff->emplace_back(start_off);
        stripe_id->emplace_back(i);
        if (remain_len > this_stripe_eoff - start_off)
        {
            stripe_len->emplace_back(this_stripe_eoff - start_off);
        }
        else
        {
            stripe_len->emplace_back(remain_len);
        }
        remain_len -= (this_stripe_eoff - start_off);
        start_off += (this_stripe_eoff - start_off);
    }
}

void StorageMod::split_chunk(uint64_t offset, uint64_t length, vector<int> *chunk_pos, vector<uint64_t> *chunk_soff, vector<uint64_t> *chunk_len)
{
    int temp;
    int stripeid_s = 0;
    int stripeid_e = 0;
    int start_chunkpos = 0;
    int end_chunkpos = 0;
    user2dev(offset, &start_chunkpos, &stripeid_s);
    user2dev(offset + length - 1, &end_chunkpos, &stripeid_e);
    assert(stripeid_s == stripeid_e);

    uint64_t start_off = offset;
    uint64_t remain_len = length;

    uint64_t stripe_soff = stripe2useroff(stripeid_s);
    int s_chunkpos = stripe2datapos(stripeid_s);

    if (end_chunkpos < start_chunkpos)
    {
        end_chunkpos += NUM_DEVFILES;
    }

    for (size_t i = start_chunkpos; i <= end_chunkpos; i++)
    {
        uint64_t this_chunk_eoff = stripe_soff + SCHUNK_SIZE * (logical_distance(s_chunkpos, i, NUM_DEVFILES) + 1);
        uint64_t devoff = user2dev(start_off, NULL, NULL);
        // printf("start_off:%ld devoff:%ld\n", start_off, devoff);
        chunk_soff->emplace_back(devoff);
        if (remain_len > this_chunk_eoff - start_off)
        {
            chunk_len->emplace_back(this_chunk_eoff - start_off);
        }
        else
        {
            chunk_len->emplace_back(remain_len);
        }

        if (i >= NUM_DEVFILES)
        {
            chunk_pos->emplace_back(i - NUM_DEVFILES);
        }
        else
        {
            chunk_pos->emplace_back(i);
        }

        remain_len = remain_len - (this_chunk_eoff - start_off);
        start_off = start_off + (this_chunk_eoff - start_off);
    }
}

vector<DIO_Info> StorageMod::split_chunk2dio(UIO_Info uio)
{
    vector<DIO_Info> ret;

    int stripeid_s = 0;
    int stripeid_e = 0;
    int start_chunkpos = 0;
    int end_chunkpos = 0;

    uint64_t offset = uio.user_offset;
    uint64_t length = uio.length;

    user2dev(offset, &start_chunkpos, &stripeid_s);
    user2dev(offset + length - 1, &end_chunkpos, &stripeid_e);
    // cout << stripeid_s << " " << stripeid_e << " " << offset << " " << length << endl;
    if (stripeid_s != stripeid_e)
    {
        cout << "split_chunk2dio " << stripeid_s << " " << stripeid_e << " " << offset << " " << length << endl;
    }

    assert(stripeid_s == stripeid_e);

    uint64_t start_off = offset;
    uint64_t remain_len = length;

    uint64_t stripe_soff = stripe2useroff(stripeid_s);
    int s_chunkpos = stripe2datapos(stripeid_s);

    if (end_chunkpos < start_chunkpos)
    {
        end_chunkpos += NUM_DEVFILES;
    }

    for (size_t i = start_chunkpos; i <= end_chunkpos; i++)
    {
        int dev_id;
        char *buf;
        uint64_t length;
        uint64_t dev_offset = user2dev(start_off, NULL, NULL);
        uint64_t this_chunk_eoff = stripe_soff + SCHUNK_SIZE * (logical_distance(s_chunkpos, i, NUM_DEVFILES) + 1);

        buf = uio.buf + start_off;

        // printf("start_off:%ld devoff:%ld\n", start_off, devoff);

        if (remain_len > this_chunk_eoff - start_off)
        {
            length = this_chunk_eoff - start_off;
        }
        else
        {
            length = remain_len;
        }

        if (i >= NUM_DEVFILES)
        {
            dev_id = i - NUM_DEVFILES;
        }
        else
        {
            dev_id = i;
        }
        DIO_Info dio(dev_id, buf, dev_offset, length);
        ret.emplace_back(dio);

        remain_len = remain_len - (this_chunk_eoff - start_off);
        start_off = start_off + (this_chunk_eoff - start_off);
    }

    return ret;
}

void StorageMod::split_stripe_aligned(UIO_Info *uio, vector<UIO_Info *> *uios_out)
{
    int usr_id = uio->user_id;
    bool iswrite = uio->is_write;
    uint64_t start_off = uio->user_offset;
    char *start_buf = uio->buf;
    uint64_t remain_len = uio->length;

    // cout << "[split_stripe_aligned] " << start_off << " " << remain_len << endl;

    int start_stripeid = 0; 
    int end_stripeid = 0;   
    user2dev(uio->user_offset, NULL, &start_stripeid);
    user2dev(uio->user_offset + uio->length - 1, NULL, &end_stripeid);

    for (int i = start_stripeid; i <= end_stripeid; i++)
    {
        uint64_t this_stripe_eoff = stripe2useroff(i) + SSTRIPE_DATASIZE;
        uint64_t this_stripe_soff = start_off;
        uint64_t this_stripe_len = this_stripe_eoff - this_stripe_soff;
        uint64_t stripe_len = 0;
        if (remain_len > this_stripe_len)
        {
            stripe_len = this_stripe_len;
        }
        else
        {
            stripe_len = remain_len;
        }

        UIO_Info *uio = new UIO_Info(usr_id, iswrite, start_buf, start_off, stripe_len);
        uios_out->emplace_back(uio);

        remain_len -= this_stripe_len;
        start_off += this_stripe_len;
        start_buf += this_stripe_len;
    }
}

void StorageMod::split_schunk(UIO_Info *uio, vector<UIO_Info *> *uios_out)
{
    vector<UIO_Info *> stripes_out;
    split_stripe_aligned(uio, &stripes_out);

    for (size_t st_cnt = 0; st_cnt < stripes_out.size(); st_cnt++)
    {
        int s_chunkpos = 0;
        int e_chunkpos = 0;
        user2dev(stripes_out[st_cnt]->user_offset, &s_chunkpos, NULL);
        user2dev(stripes_out[st_cnt]->user_offset + stripes_out[st_cnt]->length - 1, &e_chunkpos, NULL);
        if (e_chunkpos < s_chunkpos)
        {
            e_chunkpos += NUM_DEVFILES;
        }
        uint64_t start_off = stripes_out[st_cnt]->user_offset;
        uint64_t remain_len = stripes_out[st_cnt]->length;
        char *start_buf = stripes_out[st_cnt]->buf;

        for (size_t i = s_chunkpos; i <= e_chunkpos; i++)
        {
            UIO_Info *tempuio;
            if (remain_len > SCHUNK_SIZE)
            {
                tempuio = new UIO_Info(uio->user_id, uio->is_write, start_buf, start_off, SCHUNK_SIZE);
                remain_len -= SCHUNK_SIZE;
                start_off += SCHUNK_SIZE;
                start_buf += SCHUNK_SIZE;
            }
            else
            {
                tempuio = new UIO_Info(uio->user_id, uio->is_write, start_buf, start_off, remain_len);
            }
            // printf("[In split1] chunkpos:%ld, s_chunkpos:%ld, e_chunkpos:%ld\n", i, s_chunkpos, e_chunkpos);
            // printf("[In split2] User_start_off:%ld, len:%ld\n", tempuio->user_offset, tempuio->length);

            uios_out->emplace_back(tempuio);
        }
    }
}

void StorageMod::split_block(UIO_Info *uio, vector<UIO_Info *> *uios_out)
{
    int usr_id = uio->user_id;
    bool iswrite = uio->is_write;
    uint64_t start_off = uio->user_offset;
    uint64_t total_length = uio->length;
    char *start_buf = uio->buf;

    // Aligned to BLK_SIZE
    if (start_off % BLK_SIZE != 0)
    {
        start_off = start_off - (start_off % BLK_SIZE);
    }
    if (total_length % BLK_SIZE != 0)
    {
        total_length += BLK_SIZE - (total_length % BLK_SIZE);
    }

    for (size_t i = 0; i < ceil((double)total_length / BLK_SIZE); i++)
    {
        UIO_Info *io = new UIO_Info(usr_id, iswrite, start_buf, start_off, BLK_SIZE);
        uios_out->emplace_back(io);

        start_off += BLK_SIZE;
        start_buf += BLK_SIZE;
    }
}

uint64_t StorageMod::raid_write(UIO_Info uio)
{
    int thread_id = uio.user_id;
    char *buf = uio.buf;
    uint64_t user_offset = uio.user_offset;
    uint64_t length = uio.length;

    assert(buf != NULL);
    assert(user_offset >= 0);
    assert(length > 0);

    vector<UIO_Info *> v_stdblks;
    split_block(&uio, &v_stdblks);
    for (size_t i = 0; i < v_stdblks.size(); i++)
    {
        meta_mod->blk_bitmap->reset(v_stdblks[i]->user_offset / BLK_SIZE);
    }

    vector<DIO_Info> v_dios = split_chunk2dio(uio);
    if (is_fullstripe(uio.user_offset, uio.length)) // Full stripe write
    {
        s_encodemod->encode_fullstripe(thread_id, v_dios);
    }
    else // Partial stripe write
    {
        s_encodemod->encode_partialstripe(thread_id, v_dios);
    }

    return 0;
}

uint64_t StorageMod::raid_read(UIO_Info uio)
{
    assert(uio.user_offset >= 0);
    assert(uio.length > 0);

    vector<UIO_Info *> v_chunks;
    split_schunk(&uio, &v_chunks);
    vector<DIO_Info> v_schunks;
    for (size_t chunk_cnt = 0; chunk_cnt < v_chunks.size(); chunk_cnt++)
    {
        v_schunks.emplace_back(v_chunks[chunk_cnt]->s_uio2dio());
    }
    s_decodemod->s_norRead(uio.user_id, v_schunks);

    return uio.length;
}

void StorageMod::workers_run(int worker_id)
{
    // cout << "RAID worker " << worker_id << " run" << endl;

    cpu_set_t mask;                                           // CPU mask
    cpu_set_t get;                                            
    CPU_ZERO(&mask);                                          
    CPU_SET(worker_id, &mask);                                
    if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1) // set CPU affinity
    {
        printf("warning: could not set CPU affinity, continuing...\n");
        exit(-1);
    }

    cds::threading::Manager::attachThread();
    uint64_t io_count = 0;
    while (1)
    {
        auto queue = &v_workinque.at(worker_id)->uio_queue;
        UIO_Info uio;
        while (queue->try_dequeue(uio) != true)
        {
        }
        uio.user_id = worker_id;

        // cout << "IN tid: " << worker_id << " uoff: " << uio.user_offset / 1024 << " stripe: " << user2stripe(uio.user_offset) << endl;

        if (uio.is_write)
        {
            // split IO to stripes
            vector<UIO_Info *> v_suios;
            split_stripe_aligned(&uio, &v_suios);
            assert(v_suios.size() > 0);
            for (size_t i = 0; i < v_suios.size(); i++)
            {
                int stripeid = user2stripe(uio.user_offset);
                auto SST = &this->meta_mod->StdRAID_meta->sstable_mod;
                SSTEntry *sse = NULL;
                SST->search_SST(stripeid, sse);
                assert(sse != NULL);

                bool handled = false;

                while (!handled)
                {
                    bool False = false;
                    bool True = true;
                    if (sse->stripe_lock.compare_exchange_strong(False, True) == true)
                    {
                        sse->SSTthreadid.store(worker_id);
                        sse->is_frozen.store(false);

                        // cout << " begin tid: " << worker_id << " off= " << uio.user_offset << " time= " << get_nowtimeus() << " sse " << sse->SSTthreadid.load() << endl;
                        raid_write(uio);
                        // cout << " end tid: " << worker_id << " off= " << uio.user_offset << " time= " << get_nowtimeus() << " sse " << sse->SSTthreadid.load() << endl;

                        sse->SSTthreadid.store(INT16_MAX);
                        sse->stripe_lock.store(false);

                        handled = true;
                    }
                    else if (sse->is_frozen.load() == false && sse->SSTthreadid.load() < NUM_THREADS)
                    {
                        int wid = sse->SSTthreadid.load();
                        if (wid > NUM_THREADS)
                            continue;
                        DIO_Info dio = uio.s_uio2dio();
                        auto queue = &v_batchque.at(wid)->batch_queue;
                        auto queue_len = &v_batchque.at(wid)->inqueue_data_len;
                        if (sse->is_frozen.load() == false)
                        {
                            v_batchque2[wid].at(queue_len->fetch_add(1)) = dio;
                        }
                        else
                        {
                            continue;
                        }

                        while (sse->stripe_lock.load() == true)
                        {
                            sched_yield();
                        }
                        handled = true;
                    }
                }
            }
        }
        else
        {
            this->raid_read(uio);
        }
        io_count++;
    }
}

uint64_t StorageMod::raid_write_direct(UIO_Info uio)
{
    uint64_t io_count = 0;
    uint64_t worker_id = uio.user_id;

    vector<UIO_Info *> v_suios;
    split_stripe_aligned(&uio, &v_suios);
    // assert(v_suios.size() > 0);
    for (size_t ck = 0; ck < v_suios.size(); ck++)
    {
        int stripeid = user2stripe(uio.user_offset);
        auto SST = &this->meta_mod->StdRAID_meta->sstable_mod;
        SSTEntry *sse = NULL;
        SST->search_SST(stripeid, sse);
        assert(sse != NULL);

        bool handled = false;
        while (!handled)
        {
            bool False = false;
            bool True = true;
            if (sse->stripe_lock.compare_exchange_strong(False, True) == true)
            {
                sse->SSTthreadid.store(worker_id);
                sse->is_frozen.store(false);

                // cout << " begin tid: " << worker_id << " off= " << uio.user_offset << " time= " << get_nowtimeus() << " sse " << sse->SSTthreadid.load() << endl;
                raid_write(*v_suios.at(ck));
                // cout << " end tid: " << worker_id << " off= " << uio.user_offset << " time= " << get_nowtimeus() << " sse " << sse->SSTthreadid.load() << endl;

                sse->SSTthreadid.store(INT16_MAX);
                sse->stripe_lock.store(false);

                handled = true;
            }
            else if (sse->is_frozen.load() == false && sse->SSTthreadid.load() < NUM_THREADS)
            {
                int wid = sse->SSTthreadid.load();
                if (wid > NUM_THREADS)
                    continue;
                DIO_Info dio = v_suios.at(ck)->s_uio2dio();
                auto queue = &v_batchque.at(wid)->batch_queue;
                auto queue_len = &v_batchque.at(wid)->inqueue_data_len;
                if (sse->is_frozen.load() == false)
                {
                    v_batchque2[wid].at(queue_len->fetch_add(1)) = dio;
                }
                else
                {
                    continue;
                }

                while (sse->stripe_lock.load() == true)
                {
                    sched_yield();
                }
                handled = true;
            }
        }
    }
    return uio.length;
}

bool StorageMod::raid_write_q(UIO_Info uio)
{
    int select_worker = 0;
    bool handled = false;
    uint64_t waited = 0;

    auto selected_que = &v_workinque[uio.user_id]->uio_queue;
    while (selected_que->size_approx() > IDLE_QUELEN)
    {
    }
    bool ret = selected_que->try_enqueue(uio);
    assert(ret);

    return true;
}

bool StorageMod::raid_read_q(UIO_Info uio)
{
    int select_worker = 0;
    auto selected_que = &v_workinque[uio.user_id]->uio_queue;
    while (selected_que->size_approx() > IDLE_QUELEN)
    {
    }
    bool ret = selected_que->try_enqueue(uio);
    assert(ret);
    return true;
}

#endif