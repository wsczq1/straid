#ifndef KFIFO_H
#define KFIFO_H

#include <iostream>
#include <array>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <chrono>

using namespace std;
using namespace std::chrono;

struct Item
{
    uint64_t value;
    uint64_t tag;

    Item(uint64_t value)
    {
        this->value = value;
        this->tag = 0;
    }
};

class KQueue
{
public:
    uint64_t size;
    uint64_t k;

    std::atomic<uint64_t> head;
    std::atomic<uint64_t> tail;

    // TODO Idea:
    // Instead of passing around the big ass array,
    // Can we just pass around the individual segemnts?
    // Not sure if that would help.
    // This is how they do it in the paper, after all.
    array<std::atomic<uint64_t>, 10000000> arr = {};

    std::atomic<uint64_t> jobs_completed;
    std::atomic<uint64_t> failed;

    KQueue(uint64_t size, uint64_t k)
    {
        this->size = size;
        this->k = k;

        this->jobs_completed = 0;
        this->failed = 0;

        head.store(0);
        tail.store(0);
    }

    uint64_t getSize(void)
    {
        return size;
    }

    bool is_queue_full(uint64_t head_old, uint64_t tail_old)
    {
        // the queue is full when the tail wraps around and meets the head.
        // we also want to make sure the head didn't change in the meantime
        // so a second check on head occurs here as well.
        if (((tail_old + k) % size) == head_old && (head_old == head.load()))
        {
            return true;
        }
        return false;
    }

    bool findIndex(uint64_t start, bool empty, uint64_t *item_index, uint64_t *old)
    {

        uint64_t i, index, random = rand() % k;
        for (i = 0; i < k; i++)
        {
            // First find a random index from [0 - k)
            index = (start + ((random + i) % k)) % size;
            // the number that is currently in this index.
            *old = arr[index].load();
            // We assume that the index is empty if the value is 0.
            // empty just specfies if we are looking for an empty spot, or a taken spot.
            if (((empty && *old == 0)) || (!empty && *old != 0))
            {
                // both of these are pointers so that when they are changed
                // the changes can be seen in the orignal function.
                *item_index = index;
                return true;
            }
        }
        return false;
    }

    bool segment_has_stuff(uint64_t head_old)
    {
        uint64_t i, start = head_old;

        for (i = 0; i < k; i++)
        {
            if (arr[(start + i) % size].load() != 0)
            {
                return true;
            }
        }
        return false;
    }

    bool in_valid_region(uint64_t tail_old, uint64_t tail_current, uint64_t head_current)
    {
        if (tail_current < head_current)
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    bool not_in_valid_region(uint64_t tail_old, uint64_t tail_current, uint64_t head_current)
    {
        if (tail_current < head_current)
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    bool committed(uint64_t tail_old, uint64_t item_new, uint64_t index)
    {
        if (arr[tail_old].load() != item_new)
        {
            return true;
        }

        uint64_t head_curr = head.load();
        uint64_t tail_curr = tail.load();

        if (in_valid_region(tail_old, tail_curr, head_curr))
        {
            return true;
        }
        else if (not_in_valid_region(tail_old, tail_curr, head_curr))
        {
            if (!arr[index].compare_exchange_strong(item_new, 0))
            {
                return true;
            }
        }
        else
        {
            if (head.compare_exchange_strong(head_curr, head_curr))
            {
                return true;
            }
            if (!arr[index].compare_exchange_strong(item_new, 0))
            {
                return true;
            }
        }
        return false;
    }

    void move_tail_forward(uint64_t tail_old)
    {
        // TODO versioning
        tail.compare_exchange_strong(tail_old, (tail_old + k) % size);
    }

    void move_head_forward(uint64_t head_old)
    {
        // TODO versioning
        head.compare_exchange_strong(head_old, (head_old + k) % size);
    }

    bool enqueue(std::atomic<uint64_t> &new_item)
    {
        while (true)
        {
            uint64_t tail_old = tail.load();
            uint64_t head_old = head.load();

            uint64_t item_index, old;
            bool found_free_space = findIndex(tail_old, true, &item_index, &old);

            if (tail_old == tail.load())
            {
                if (found_free_space)
                {
                    // TODO - implement version numbering. This would mean using atomic struct pointers.
                    // Not sure what the implications of this would be, need to think about it.
                    // printf("Got call to enqueue. Found free space at %ld with value %ld\n", item_index, old);

                    if (arr[item_index].compare_exchange_strong(old, new_item))
                    {
                        if (committed(tail_old, new_item, item_index))
                        {
                            return true;
                        }
                    }
                }
                else
                {
                    if (is_queue_full(head_old, tail_old))
                    {
                        // If our head segment has stuff, it means we are full.
                        if (segment_has_stuff(head_old) && head_old == head.load())
                        {
                            return false;
                        }

                        move_head_forward(head_old);
                    }

                    // check if queue is full AND the segemnt
                    move_tail_forward(tail_old);
                }
            }
        }
    }

    bool dequeue(uint64_t *item)
    {
        while (true)
        {
            uint64_t head_old = head.load();

            uint64_t item_index, old;
            bool found_index = findIndex(head_old, false, &item_index, &old);

            uint64_t tail_old = tail.load();

            if (head_old == head.load())
            {
                // we found something!
                if (found_index)
                {
                    //  we don't want to be enqueing/dequeing from the same segment!
                    if (head_old == tail_old)
                    {
                        move_tail_forward(tail_old);
                    }

                    if (arr[item_index].compare_exchange_strong(old, 0))
                    {
                        *item = old;
                        return true;
                    }
                }
                else
                {
                    if ((head_old == tail_old) && (tail_old == tail.load()))
                    {
                        return false;
                    }

                    move_head_forward(head_old);
                }
            }
        }
    }

    void printQueue()
    {
        uint64_t i;
        for (i = head.load(); i <= tail.load() + k - 1; i++)
        {
            if (i % k == 0)
            {
                printf(" - ");
            }
            printf("%ld, ", arr[i].load());
        }
        printf("\n");
    }

    void do_work(uint64_t thread_number, std::atomic<uint64_t> items_to_add[], uint64_t length, bool deq, bool enq)
    {
        uint64_t i, dequeued_value;
        uint64_t count = 0;

        for (uint64_t i = 0; i < length; i++)
        {
            count += items_to_add[i];
        }

        for (i = 0; i < length; i++)
        {
            uint64_t randy = rand() % 2;
            if (randy == 0 && enq)
            {
                // printf("#%ld    ---------------------enq(%ld)----------------------- %ld %ld\n", thread_number, items_to_add[i].load(), head.load(), tail.load());
                bool s = enqueue(items_to_add[i]);
                this->jobs_completed++;
            }

            if (randy == 1 && deq)
            {
                // printf("#%ld    ---------------------deq()----------------------- %ld %ld\n", thread_number, head.load(), tail.load());
                bool s = dequeue(&dequeued_value);
                this->jobs_completed++;
            }
        }
    }
};

#endif