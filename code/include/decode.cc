#ifndef DECODE_CC
#define DECODE_CC

#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <memory.h>
#include "metadata.h"
#include "decode.h"

// #define DEGRADE
extern atomic_bool proc_end_flag;

uint64_t SDecodeMod::s_norRead(int thread_id, vector<DIO_Info> v_dios)
{
#ifndef DEGRADE
    if (v_dios.size() == 0)
    {
        return 0;
    }
    uint64_t total_read_size = 0;
    for (size_t ios = 0; ios < v_dios.size(); ios++)
    {
        DIO_Info dio = v_dios.at(ios);

        DevFile *destdev = v_stdfiles->at(dio.dev_id);
        int fd = destdev->file_fd;
        // printf("Read STD | threadID:%d, fd:%d, devoff:%ld, len:%ld\n", thread_id, fd, dio.dev_offset, dio.length);
        iouring_rprep(&stdring[thread_id], fd, dio.buf, dio.dev_offset, dio.length);
        total_read_size += dio.length;
    }
    uint64_t ret = io_uring_submit(&stdring[thread_id]);
    iouring_wait(&stdring[thread_id], ret);

#else

    if (v_dios->size() == 0)
    {
        return 0;
    }

    uint64_t total_read_size = 0;
    for (size_t ios = 0; ios < v_dios->size(); ios++)
    {
        DIO_Info *dio = v_dios->at(ios);

        if (dio->dev_id == 0)
        {
            vector<char *> degrade_buf;
            for (size_t i = 0; i < DATACHUNK_NUM + PARITYCHUNK_NUM; i++)
            {
                DevFile *destdev = v_stdfiles->at(i);
                int fd = destdev->file_fd;

                char *ptr = NULL;
                int ret = posix_memalign((void **)&ptr, ALIGN_SIZE, dio->length);
                assert(ret == 0);
                degrade_buf.emplace_back(ptr);

                iouring_rprep(&stdring[thread_id], fd, ptr, dio->dev_offset, dio->length);
            }
            uint64_t ret = io_uring_submit(&stdring[thread_id]);
            iouring_wait(&stdring[thread_id], ret);

            for (size_t i = 0; i < DATACHUNK_NUM + 1; i++)
            {
                // cout << thread_id << endl;
                ecEncoder *ecdecoder = v_ecDecoder[thread_id];
                uint64_t encode_len = dio->length;
                vector<int> errs_list;
                errs_list.emplace_back(0);
                vector<char *> recovered;
                // ecdecoder->do_decode(degrade_buf, errs_list, &recovered);
                usleep(1);
            }
        }
        else
        {
            DevFile *destdev = v_stdfiles->at(dio->dev_id);
            int fd = destdev->file_fd;
            // printf("Read STD | threadID:%d, fd:%d, devoff:%ld, len:%ld\n", thread_id, fd, dio->dev_offset, dio->length);
            iouring_rprep(&stdring[thread_id], fd, dio->buf, dio->dev_offset, dio->length);
            total_read_size += dio->length;
        }
    }
    uint64_t ret = io_uring_submit(&stdring[thread_id]);
    iouring_wait(&stdring[thread_id], ret);

#endif

    return total_read_size;
}

#endif