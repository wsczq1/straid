#ifndef TOOLS_CC
#define TOOLS_CC

#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include "tools.h"
#include <sched.h>
#include <assert.h>
#include <numeric>

atomic_uint64_t ring_pending[100] = {0};
atomic_bool proc_end_flag(false);

struct timespec s_latency_clock[100];
struct timespec e_latency_clock[100];
vector<uint64_t> IO_LatArray[100];
vector<uint64_t> Wait_LatArray[100];
vector<uint64_t> Cachesearch_LatArray[100];
vector<uint64_t> Parityread_LatArray[100];
vector<uint64_t> DATAwrite_LatArray[100];

struct timespec start_clock[100];
struct timespec end_clock[100];

struct timespec s_persist_clock[100];
struct timespec e_persist_clock[100];

atomic_uint64_t All_Write_Data = 0; // Workload written data
atomic_uint64_t All_Read_Data = 0;  // Workload read data
atomic_uint64_t Batch_Count(0);
atomic_uint64_t Block_Count(0);
atomic_uint64_t Cache_Hit(0);
atomic_uint64_t Cache_Miss(0);

bool isBadPtr(void *p)
{
    int fh = open((const char *)p, 0, 0);
    int e = errno;

    if (-1 == fh && e == EFAULT)
    {
        return true;
    }
    else if (fh != -1)
    {
        close(fh);
    }
    return false;
}

void dump(unsigned char *buf, int len)
{
    int i;
    for (i = 0; i < len;)
    {
        printf(" %2x", 0xff & buf[i++]);
        if (i % 32 == 0)
            printf("\n");
    }
    printf("\n");
}

uint64_t get_nowtimeus()
{
    timeval time;
    gettimeofday(&time, NULL);
    return time.tv_usec;
}

void tic(int i)
{
    clock_gettime(CLOCK_ID, &start_clock[i]);
}
double toc(int i)
{
    clock_gettime(CLOCK_ID, &end_clock[i]);

    unsigned long time_use = (end_clock[i].tv_sec - start_clock[i].tv_sec) * 1000000000 + (end_clock[i].tv_nsec - start_clock[i].tv_nsec);
    return time_use * 1.0;
}

void ptic(int i)
{
    clock_gettime(CLOCK_ID, &s_persist_clock[i]);
}
uint64_t ptoc(int i)
{
    clock_gettime(CLOCK_ID, &e_persist_clock[i]);
    unsigned long time_use = (e_persist_clock[i].tv_sec - s_persist_clock[i].tv_sec) * 1000000000 + (e_persist_clock[i].tv_nsec - s_persist_clock[i].tv_nsec);
    return time_use;
}

void lat_tic(int i)
{
    clock_gettime(CLOCK_ID, &s_latency_clock[i]);
}
uint64_t lat_toc(int i)
{
    clock_gettime(CLOCK_ID, &e_latency_clock[i]);
    unsigned long time_use = (e_latency_clock[i].tv_sec - s_latency_clock[i].tv_sec) * 1000000000 + (e_latency_clock[i].tv_nsec - s_latency_clock[i].tv_nsec);
    IO_LatArray[i].emplace_back(time_use);
    return time_use;
}

uint64_t comm_tic(timespec *start_time)
{
    return clock_gettime(CLOCK_ID, start_time);
}
uint64_t comm_toc(timespec *start_time, timespec *end_time)
{
    clock_gettime(CLOCK_ID, end_time);
    unsigned long time_use = (end_time->tv_sec - start_time->tv_sec) * 1000000000 + (end_time->tv_nsec - start_time->tv_nsec);
    return time_use;
}

template <typename T>
T SumVector(vector<T> &vec)
{
    T res = 0;
    for (size_t i = 0; i < vec.size(); i++)
    {
        res += vec[i];
    }
    return res;
}

vector<uint64_t> *merge_IOLat(int thread_count)
{
    vector<uint64_t> *v_sum = new vector<uint64_t>();
    for (size_t i = 0; i < thread_count; i++)
    {
        v_sum->insert(v_sum->end(), IO_LatArray[i].begin(), IO_LatArray[i].end());
    }
    sort(v_sum->begin(), v_sum->end());
    return v_sum;
}

vector<uint64_t> *show_IOLat(int thread_count)
{
    vector<uint64_t> *v_sum = merge_IOLat(thread_count);

    vector<float> percentiles = {0.50, 0.90, 0.95, 0.99, 0.999, 0.9999};
    vector<uint64_t> *lat_ret = new vector<uint64_t>();
    uint64_t avg_result = SumVector(*v_sum) / v_sum->size();
    printf("Percentiles  |  latency(ns)\n");
    printf("%s \t %ld \n", "Average", avg_result);
    lat_ret->emplace_back(avg_result);
    for (size_t i = 0; i < percentiles.size(); i++)
    {
        uint point = (uint)(percentiles[i] * v_sum->size());
        lat_ret->emplace_back(v_sum->at(point));

        printf("%f \t %ld \n", percentiles[i], v_sum->at(point));
    }

    return lat_ret;
}

void clear_IOLat()
{
    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        IO_LatArray[i].clear();
    }
}

pair<float, float> print_throughtput(long long data_length, int io_count, double past_time, const char *info)
{
    // printf("%lf,%lld\n",past_time,data_length);
    past_time = past_time / 1000000000;
    data_length = data_length / 1024 / 1024;
    // printf("%lf,%lld\n",past_time,data_length);

    printf("[%s] | "
           "Throughtput: %.2lf MB/s | "
           "IOPS: %.2f | "
           "Time used: %.2f ms\n",
           info,
           data_length / past_time,
           float(io_count / past_time),
           (past_time * 1000));

    return make_pair(float(data_length / past_time), float(io_count / past_time));
}

int DropCaches(int drop)
{
    int ret = 0;
#ifdef WIN32

#else
    int fd = 0;
    fd = open("/proc/sys/vm/drop_caches", O_RDWR);
    if (fd < 0)
    {
        return -1;
    }
    char dropData[32] = {0};
    int dropSize = snprintf(dropData, sizeof(dropData), "%d", drop);

    ret = write(fd, dropData, dropSize);
    close(fd);
#endif
    return ret;
}

vector<uint64_t> randperm(uint64_t Num)
{
    vector<uint64_t> temp;
    for (uint64_t i = 0; i < Num; ++i)
    {
        temp.emplace_back(i + 1);
    }
    random_shuffle(temp.begin(), temp.end());

    return temp;
}

atomic_uint64_t uring_error(0);

void iouring_wprep(io_uring *ring, int fd, char *buf, uint64_t dev_off, uint64_t length)
{
    All_Write_Data.fetch_add(length);
    io_uring_sqe *sqe = NULL;
    sqe = io_uring_get_sqe(ring);
    iovec *iov = (iovec *)malloc(sizeof(iovec));
    iov->iov_base = buf;
    iov->iov_len = length;
    io_uring_prep_writev(sqe, fd, iov, 1, dev_off);
}

void iouring_rprep(io_uring *ring, int fd, char *buf, uint64_t dev_off, uint64_t length)
{
    All_Read_Data.fetch_add(length);
    io_uring_sqe *sqe = NULL;
    sqe = io_uring_get_sqe(ring);
    iovec *iov = (iovec *)malloc(sizeof(iovec));
    iov->iov_base = buf;
    iov->iov_len = length;
    io_uring_prep_readv(sqe, fd, iov, 1, dev_off);
}

uint64_t iouring_wsubmit(io_uring *ring, int fd, char *buf, uint64_t dev_off, uint64_t length)
{
    io_uring_sqe *sqe = io_uring_get_sqe(ring);
    iovec *iov = (iovec *)malloc(sizeof(iovec));
    iov->iov_base = buf;
    iov->iov_len = length;

    io_uring_prep_writev(sqe, fd, iov, 1, dev_off);
    uint64_t ret = io_uring_submit(ring);

    return ret;
}

uint64_t iouring_rsubmit(io_uring *ring, int fd, char *buf, uint64_t dev_off, uint64_t length)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    assert(sqe != NULL);
    struct iovec iov = {
        .iov_base = buf,
        .iov_len = length,
    };
    io_uring_prep_readv(sqe, fd, &iov, 1, dev_off);
    uint64_t ret = io_uring_submit(ring);

    return ret;
}

bool iouring_wait(io_uring *ring, uint wait_count)
{
    for (uint i = 0; i < wait_count; i++)
    {
        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(ring, &cqe);
        io_uring_cqe_seen(ring, cqe);
    }
    return true;
}

uint64_t o_align(uint64_t offset, uint64_t align)
{
    return offset - (offset % align);
}

uint64_t l_align(uint64_t length, uint64_t align)
{
    return ((length / align) + 1) * align;
}

#endif