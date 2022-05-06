#ifndef ECENCODER_H
#define ECENCODER_H

#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <mutex>

#include "define.h"
#include "tools.h"
#include "isa-l.h"

using namespace std;

void usage(void);

class ecEncoder
{
public:
    int k;   // data chunk num
    int p;   // parity chunk num
    int len; // chunk size
    int nerrs = 0;

public:
    vector<char *> v_tempbuf;

public:
    ecEncoder(int num_data_chunks, int num_parity_chunks, int chunk_size)
    {
        k = num_data_chunks;
        p = num_parity_chunks;
        len = chunk_size;
        // printf(" Encoder (m,k,p)=(%d,%d,%d) len=%d\n", m, k, p, len);

        // Check for valid parameters
        if (k > KMAX || p < 1 || k < 1)
        {
            usage();
            exit(1);
        }

        for (size_t i = 0; i < 100; i++)
        {
            void *buf;
            int ret = posix_memalign(&buf, 512, len);
            if (ret)
            {
                printf("alloc error: Fail");
                exit(-2);
            }
            v_tempbuf.emplace_back((char *)buf);
        }
    };

    bool do_full_encode(vector<char *> databufs, vector<char *> paritybufs)
    {
        char *buffs[DATACHUNK_NUM + PARITYCHUNK_NUM];
        for (size_t i = 0; i < DATACHUNK_NUM; i++)
        {
            buffs[i] = databufs.at(i);
        }
        for (size_t i = 0; i < PARITYCHUNK_NUM; i++)
        {
            buffs[i + DATACHUNK_NUM] = paritybufs.at(i);
        }

        if (PARITYCHUNK_NUM == 1)
        {
            xor_gen(DATACHUNK_NUM + PARITYCHUNK_NUM, SCHUNK_SIZE, (void **)buffs);
        }
        else if (PARITYCHUNK_NUM == 2)
        {
            pq_gen(DATACHUNK_NUM + PARITYCHUNK_NUM, SCHUNK_SIZE, (void **)buffs);
        }

        return true;
    }

    bool do_part_encode(vector<char *> oridatabufs, vector<char *> newdatabufs, vector<char *> paritybufs,
                        vector<uint64_t> offsets, vector<uint64_t> lengths)
    {
        assert(oridatabufs.size() == newdatabufs.size());
        size_t chunknums = offsets.size();

        if (PARITYCHUNK_NUM == 1)
        {
            for (size_t i = 0; i < chunknums; i++)
            {
                // cout << "do_part_encode " << offsets[i] << " " << lengths[i] << " len " << lengths.size() << endl;
                if(offsets[i] + lengths[i] > len) break;
                char *databuffs[3];
                databuffs[0] = oridatabufs[i];
                databuffs[1] = newdatabufs[i];
                databuffs[2] = v_tempbuf[i];
                xor_gen(3, lengths[i], (void **)databuffs);

                char *buffs[3];
                buffs[0] = v_tempbuf[i];
                buffs[1] = paritybufs[0] + offsets[i];
                buffs[2] = paritybufs[0] + offsets[i];
                xor_gen(3, lengths[i], (void **)buffs);
            }
        }
        else if (PARITYCHUNK_NUM == 2)
        {
            for (size_t i = 0; i < chunknums; i++)
            {
                assert(offsets[i] + lengths[i] <= len);
                char *databuffs[3];
                databuffs[0] = oridatabufs[i];
                databuffs[1] = newdatabufs[i];
                databuffs[2] = v_tempbuf[i];
                xor_gen(3, lengths[i], (void **)databuffs);

                char *buffs[5];
                buffs[0] = v_tempbuf[i];
                buffs[1] = paritybufs[0] + offsets[i];
                buffs[2] = paritybufs[1] + offsets[i];
                buffs[3] = paritybufs[0] + offsets[i];
                buffs[4] = paritybufs[1] + offsets[i];
                pq_gen(5, lengths[i], (void **)buffs);
            }
        }
        return true;
    }
};

#endif