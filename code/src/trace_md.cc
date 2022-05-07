#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <string>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <atomic>

#include "define.h"
#include "worker.h"
#include "split.h"
#include "tools.h"

using namespace std;

extern vector<uint64_t> IO_LatArray[];

extern atomic_uint64_t All_Write_Data;
extern atomic_uint64_t All_Read_Data;

const int G_NUM_WORKERS = NUM_THREADS;

atomic_uint64_t G_DATA_WRITTEN(0);
atomic_uint64_t G_DATA_READ(0);
atomic_uint64_t G_IOCOUNT(0);
vector<double> G_IOLAT[G_NUM_WORKERS];

atomic_uint64_t iodir_cnt(0);
atomic_uint64_t off_cnt(0);
atomic_uint64_t len_cnt(0);

uint64_t MAX_LoadOff(0);

bool Collector_endflag = false;

struct TTest_Item
{
    uint64_t all_data_written;
    uint64_t all_data_read;
    uint64_t all_iocount;

    vector<uint64_t> IOPS_persec;
    vector<uint64_t> Band_persec;
    vector<uint64_t> WBand_persec;

    vector<uint64_t> all_IOLat_list;
};

void worker_run(int thread_id,
                int fd,
                vector<bool> *iodir_l,
                vector<uint64_t> *off_l,
                vector<uint64_t> *len_l,
                uint64_t count)
{

    char *buf;
    int ret = posix_memalign((void **)&buf, ALIGN_SIZE, 64 * MB);
    assert(ret == 0);

    for (size_t io = 0; io < count; io++)
    {
        uint64_t offset = off_l->at(off_cnt.fetch_add(1));
        uint64_t length = len_l->at(len_cnt.fetch_add(1));
        bool iodir = iodir_l->at(iodir_cnt.fetch_add(1));

        // Align
        if (offset > USER_SPACE_LEN)
        {
            offset = offset % USER_SPACE_LEN;
        }
        if ((offset % SECTOR_SIZE) != 0)
        {
            offset = offset - (offset % (SECTOR_SIZE)) + SECTOR_SIZE;
        }
        if ((length % SECTOR_SIZE) != 0)
        {
            length = length - (length % (SECTOR_SIZE)) + SECTOR_SIZE;
        }

        tic(10 + thread_id);
        if (iodir == 0)
        {
            uint64_t rret = pread64(fd, buf, length, offset);
            // perror("pread64");
            assert(rret == length);
            G_DATA_READ.fetch_add(rret);
        }
        else
        {
            uint64_t wret = pwrite64(fd, buf, length, offset);
            // perror("pwrite64");
            assert(wret == length);
            G_DATA_WRITTEN.fetch_add(wret);
        }
        G_IOCOUNT.fetch_add(1);
        double time = toc(10 + thread_id);
        IO_LatArray[thread_id].emplace_back(time);
    }

    return;
}

void collector_run(TTest_Item *citem)
{
    uint64_t last_written = G_DATA_WRITTEN.load();
    uint64_t last_read = G_DATA_READ.load();
    uint64_t last_iocnt = G_IOCOUNT.load();
    while (!Collector_endflag)
    {
        sleep(1);
        uint64_t now_written = G_DATA_WRITTEN.load();
        uint64_t now_read = G_DATA_READ.load();
        uint64_t now_iocnt = G_IOCOUNT.load();

        citem->WBand_persec.emplace_back(now_written - last_written);
        citem->Band_persec.emplace_back(now_written + now_read - last_written - last_read);
        citem->IOPS_persec.emplace_back(now_iocnt - last_iocnt);

        last_written = now_written;
        last_read = now_read;
        last_iocnt = now_iocnt;
    }

    vector<uint64_t> *temp = merge_IOLat(G_NUM_WORKERS);
    citem->all_IOLat_list.insert(citem->all_IOLat_list.end(), temp->begin(), temp->end());

    citem->all_data_written = G_DATA_WRITTEN.load();
    citem->all_data_read = G_DATA_READ.load();
    citem->all_iocount = G_IOCOUNT.load();

    return;
}

int main(int argc, char *argv[])
{
    uint64_t RAID_type;
    char *RAID_path;
    if (argc > 2)
    {
        RAID_type = atoi(argv[1]);
        RAID_path = argv[2];
        cout << "RAID_path " << RAID_path << " RAID_type RAID-" << RAID_type << endl;
    }
    else
    {
        cout << "Need MD RAID type and RAID path" << endl;
        exit(-1);
    }

    ofstream outfile;
    outfile.open("./results/MD_trace_results.txt", ios::out | ios::app);
    if (!outfile.is_open())
    {
        cout << "outfile open error" << endl;
        exit(-1);
    }

    // MD RAID
    printf("Open Linux MD Device\n");
    int RAID5_fd = -1;
    int RAID6_fd = -1;
    if (RAID_type == 5)
    {
        RAID5_fd = open(RAID_path, O_RDWR | O_DIRECT | O_TRUNC);
        assert(RAID5_fd >= 0);
    }
    else if (RAID_type == 6)
    {
        RAID6_fd = open(RAID_path, O_RDWR | O_DIRECT | O_TRUNC);
        assert(RAID6_fd >= 0);
    }
    else
    {
        cout << "error RAID level" << endl;
        exit(0);
    }

    // Trace Files
    printf("Open Trace Files\n");
    string tfile = "./Traces/fileserver_1.log";
    vector<string> v_tfileset{tfile};
    vector<ifstream *> v_tracefile;
    for (size_t i = 0; i < v_tfileset.size(); i++)
    {
        ifstream *file = new ifstream();
        file->open(v_tfileset[i].c_str(), ios::in);
        if (!file->is_open())
        {
            cout << "file open error" << endl;
            exit(-1);
        }
        v_tracefile.emplace_back(file);
    }

    for (size_t traces = 0; traces < v_tfileset.size(); traces++)
    {
        All_Write_Data.store(0);
        All_Read_Data.store(0);
        TTest_Item this_citem;
        uint64_t asize = SSTRIPE_DATASIZE;

        printf("Run Trace %s\n", v_tfileset.at(traces).c_str());
        vector<bool> trace_iodir;
        vector<uint64_t> trace_off;
        vector<uint64_t> trace_len;

        string line;
        uint64_t lineCount = 0;
        while (getline(*v_tracefile.at(traces), line))
        {
            if (line.empty())
                continue;
            lineCount++;
            vector<string> lineSplit;
            str_split(line, lineSplit, "\t");
            uint64_t offset = atoll(lineSplit[1].c_str());
            uint64_t length = atoll(lineSplit[2].c_str());
            ol_align(length, offset, BLK_SIZE);
            if (lineSplit[0] == "W" && length > asize)
            {
                ol_align(length, offset, asize);
            }

            if (length > 100 * MB)
            {
                continue;
            }
            trace_iodir.emplace_back(lineSplit[0] == "R" ? 0 : 1);
            trace_off.emplace_back(offset);
            trace_len.emplace_back(length);
        }
        cout << "[Trace] lineCount = " << lineCount << endl;
        MAX_LoadOff = *max_element(trace_off.begin(), trace_off.end());

        printf("Testing Trace\n");
        tic(0);
        thread worker_tid[G_NUM_WORKERS];
        for (size_t th = 0; th < G_NUM_WORKERS; th++)
        {
            vector<bool>::const_iterator iodir_itr = trace_iodir.begin() + th * (trace_iodir.size() / G_NUM_WORKERS);
            vector<uint64_t>::const_iterator off_itr = trace_off.begin() + th * (trace_off.size() / G_NUM_WORKERS);
            vector<uint64_t>::const_iterator len_itr = trace_len.begin() + th * (trace_len.size() / G_NUM_WORKERS);
            worker_tid[th] = thread(worker_run,
                                    th,
                                    RAID_type == 5 ? RAID5_fd : RAID6_fd,
                                    &trace_iodir,
                                    &trace_off,
                                    &trace_len,
                                    (trace_iodir.size() / G_NUM_WORKERS));
        }

        thread collector_tid;
        collector_tid = thread(collector_run, &this_citem);
        for (size_t th = 0; th < G_NUM_WORKERS; th++)
        {
            worker_tid[th].join();
        }
        Collector_endflag = true;
        collector_tid.join();

        double timer = toc(0);
        print_throughtput(0, trace_off.size(), timer, v_tfileset.at(traces).c_str());

        printf("Printing Results\n");
        outfile << v_tfileset.at(traces) << endl;

        // outfile << "All_Write_Data"
        //         << "\t" << All_Write_Data.load() << endl;
        // outfile << "All_Read_Data"
        //         << "\t" << All_Read_Data.load() << endl;

        outfile << "Band Persec: "
                << endl;
        for (size_t i = 0; i < this_citem.Band_persec.size() / 1; i++)
        {
            outfile << this_citem.Band_persec.at(i) / 1024 / 1024 << "\t";
        }
        outfile << endl;

        outfile << "Write Band Persec: "
                << endl;
        for (size_t i = 0; i < this_citem.WBand_persec.size() / 1; i++)
        {
            outfile << this_citem.WBand_persec.at(i) / 1024 / 1024 << "\t";
        }
        outfile << endl;

        outfile << "IOPS Persec: "
                << endl;
        for (size_t i = 0; i < this_citem.IOPS_persec.size() / 1; i++)
        {
            outfile << this_citem.IOPS_persec.at(i) << "\t";
        }
        outfile << endl;

        outfile << "Latancy CDF: " << endl;
        for (size_t i = 0; i < 1000; i++)
        {
            float percent = (float)(i * 0.001);
            uint64_t pos = percent * this_citem.all_IOLat_list.size();
            outfile << this_citem.all_IOLat_list.at(pos) << "\t";
        }
        outfile << endl;
        outfile << endl;

        G_DATA_WRITTEN.store(0);
        G_DATA_READ.store(0);
        G_IOCOUNT.store(0);
        for (size_t i = 0; i < G_NUM_WORKERS; i++)
        {
            IO_LatArray[i].clear();
        }
        Collector_endflag = false;

        iodir_cnt.store(0);
        off_cnt.store(0);
        len_cnt.store(0);
    }

    return 0;
}
