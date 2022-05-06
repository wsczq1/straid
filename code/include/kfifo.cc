#ifndef KFIFO_CC
#define KFIFO_CC

#include <iostream>
#include <array>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <chrono>

#include "kfifo.h"

// note - some of this code was inspired by the algorithms located at
// https://github.com/scala/scala
// nothing was directly taken.


// int main()
// {
    // Initialize an array of atomic integers to 0.
    // int i, j;

    // KQueue *qPointer = new KQueue(10000000, 32);
    // int dequeued_value;
    // int start_index = 0;

    // // This is the only paramater that should be modified!
    // int num_threads = 100;

    // int total_jobs = 10000000;
    // int jobs_per_thread = total_jobs / num_threads;
    // int elems = 0;

    // std::thread t[num_threads];
    // // Create nodes before hand. This queue requires that
    // // only unique items are added!
    // atomic<int> **pre = new atomic<int> *[num_threads];
    // for (i = 0; i < num_threads; i++)
    // {
    //     pre[i] = new atomic<int>[jobs_per_thread];
    //     for (j = 0; j < jobs_per_thread; j++)
    //     {
    //         pre[i][j] = start_index + j;
    //         elems++;
    //     }
    //     start_index += jobs_per_thread;
    // }

    // for (i = 0; i < num_threads; i++)
    // {
    //     t[i] = std::thread(&KQueue::do_work, qPointer, i, pre[i], jobs_per_thread, true, true);
    // }

    // high_resolution_clock::time_point t1 = high_resolution_clock::now();
    // for (int i = 0; i < num_threads; i++)
    // {
    //     t[i].join();
    // }

    // high_resolution_clock::time_point t2 = high_resolution_clock::now();
    // auto duration = duration_cast<milliseconds>(t2 - t1).count();

    // printf("\nTime elapsed in ms: %lld\n", duration);

    // // qPointer->printQueue();

    // return 0;
// }

#endif