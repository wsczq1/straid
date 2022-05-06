#ifndef QUEUES_CC
#define QUEUES_CC

#include "queues.h"

bool is_all_queue_empty(V_DevQue *v_devques)
{
    size_t len = v_devques->size();
    for (size_t i = 0; i < len; i++)
    {
        size_t remainlen = v_devques->at(i)->dev_queue->size_approx();
        if (remainlen > 0)
        {
            // printf("Waiting for queue [%ld] to end, size [%ld]\n", i, remainlen);
            return false;
        }
    }
    return true;
}

#endif