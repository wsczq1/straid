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

#include <cds/init.h>
#include <cds/threading/model.h>
#include <cds/container/feldman_hashmap_dhp.h>
#include <cds/gc/hp.h>

#include "tools.h"
#include "worker.h"
#include "encode.h"
#include "decode.h"
#include "logfiles.h"
#include "storageMod.h"
#include "metadata.h"
#include "split.h"
#include "define.h"

using namespace std;

extern vector<uint64_t> IO_LatArray[];

extern atomic_uint64_t All_Write_Data;
extern atomic_uint64_t All_Read_Data;

extern atomic_uint64_t Cache_Hit;
extern atomic_uint64_t Cache_Miss;

const int G_NUM_WORKERS = NUM_THREADS;

atomic_uint64_t iodir_cnt(0);
atomic_uint64_t off_cnt(0);
atomic_uint64_t len_cnt(0);

atomic_uint64_t G_DATA_WRITTEN(0);
atomic_uint64_t G_DATA_READ(0);
atomic_uint64_t G_IOCOUNT(0);
vector<double> G_IOLAT[G_NUM_WORKERS];

extern StorageMod *GloStor;
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
                StorageMod *storagemod,
                vector<bool> *iodir_l,
                vector<uint64_t> *off_l,
                vector<uint64_t> *len_l,
                // vector<bool>::const_iterator iodir_itr,
                // vector<uint64_t>::const_iterator off_itr,
                // vector<uint64_t>::const_iterator len_itr,
                uint64_t count)
{
    cds::threading::Manager::attachThread();
    char *buf;
    int ret = posix_memalign((void **)&buf, ALIGN_SIZE, 100 * MB);
    assert(ret == 0);

    uint64_t offset = 0;
    uint64_t length = 0;
    bool iodir = 0;

    for (size_t io = 0; io < count; io++)
    {
        offset = off_l->at(off_cnt.fetch_add(1));
        length = len_l->at(len_cnt.fetch_add(1));
        iodir = iodir_l->at(iodir_cnt.fetch_add(1));

        // uint64_t offset = *off_itr++;
        // uint64_t length = *len_itr++;
        // bool iodir = *iodir_itr++;

        if (offset > USER_SPACE_LEN)
        {
            offset = offset % USER_SPACE_LEN;
        }
        if ((offset % ALIGN_SIZE) != 0)
        {
            offset = offset - (offset % (ALIGN_SIZE)) + ALIGN_SIZE;
        }
        if ((length % ALIGN_SIZE) != 0)
        {
            length = length - (length % (ALIGN_SIZE)) + ALIGN_SIZE;
        }

        // cout << io << " off: " << offset << "  len: " << length << endl;

        tic(10 + thread_id);
        if (iodir == 0)
        {
            UIO_Info iometa(thread_id, false, buf, offset, length);
            storagemod->raid_read(iometa);
            G_DATA_READ.fetch_add(length);
        }
        else
        {
            UIO_Info iometa(thread_id, true, buf, offset, length);
            storagemod->raid_write_direct(iometa);
            G_DATA_WRITTEN.fetch_add(length);
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

        // cout << now_written << " " << now_read << " " << now_iocnt << endl;
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
    string trace_file;
    if (argc > 1)
    {
        trace_file = argv[1];
    }

    ofstream outfile;
    outfile.open("./results/ST_trace_results.txt", ios::out | ios::app);
    if (!outfile.is_open())
    {
        cout << "outfile open error" << endl;
        exit(-1);
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

    cout << "Init RAID System" << endl;
    cout << dec << "Number of SSDs: " << NUM_DEV << " | Data Chunks: " << DATACHUNK_NUM << " | Parity Chunks: " << PARITYCHUNK_NUM << endl;
    cout << dec << "Number of Workers: " << G_NUM_WORKERS << endl;
    assert(NUM_DEV == (DATACHUNK_NUM + PARITYCHUNK_NUM));
    srand(time(0));

    string lfile0 = "/dev/nvme0n1p4";
    string lfile1 = "/dev/nvme1n1p4";
    string lfile2 = "/dev/nvme2n1p4";
    string lfile3 = "/dev/nvme3n1p4";
    string lfile4 = "/dev/nvme4n1p4";
    string lfile5 = "/dev/nvme5n1p4";

    // string lfile0 = "/dev/ram0";
    // string lfile1 = "/dev/ram1";
    // string lfile2 = "/dev/ram2";
    // string lfile3 = "/dev/ram3";
    // string lfile4 = "/dev/ram4";
    // string lfile5 = "/dev/ram5";
    vector<string> v_fileset{lfile0, lfile1, lfile2, lfile3, lfile4, lfile5};
    assert(NUM_DEV <= v_fileset.size());

    cout << "Open Files" << endl;
    vector<int> v_logfd;
    for (size_t i = 0; i < v_fileset.size(); i++)
    {
        int fd = open(v_fileset[i].c_str(), O_RDWR | O_DIRECT | O_TRUNC);
        assert(fd != -1);
        v_logfd.emplace_back(fd);
    }

    cout << "Generating DevFile" << endl;
    V_DevFiles v_stdFiles;
    for (size_t i = 0; i < (NUM_DEVFILES); i++)
    {
        DevFile *devfile = new DevFile(i, v_fileset[i], v_logfd[i], 0, STRA_SPACE_LEN);
        v_stdFiles.emplace_back(devfile);
    }

    uint64_t user_start_offset = 0;
    uint64_t user_end_offset = USER_SPACE_LEN;
    cout << "Generating MetaMod" << endl;
    MetaMod metamod(user_start_offset, user_end_offset, &v_stdFiles);
    cout << "Generating StorageMod" << endl;
    StorageMod storagemod(&v_stdFiles, &metamod);
    GloStor = &storagemod;

    for (size_t traces = 0; traces < v_tracefile.size(); traces++)
    {
        All_Write_Data.store(0);
        All_Read_Data.store(0);
        Cache_Hit.store(0);
        Cache_Miss.store(0);

        TTest_Item this_citem;

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
            offset = o_align(offset, SCHUNK_SIZE);
            if (lineSplit[0] == "W" && length > SSTRIPE_DATASIZE)
            {
                offset = o_align(offset, SSTRIPE_DATASIZE);
                length = l_align(length, SSTRIPE_DATASIZE);
            }

            if (length > 100 * MB)
            {
                continue;
            }

            trace_iodir.emplace_back(lineSplit[0] == "R" ? 0 : 1);
            trace_off.emplace_back(offset);
            trace_len.emplace_back(length);
        }
        cout << "[Reading] lineCount = " << lineCount << endl;

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
                                    &storagemod,
                                    &trace_iodir,
                                    &trace_off,
                                    &trace_len,
                                    // iodir_itr,
                                    // off_itr,
                                    // len_itr,
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

        outfile << "All_Write_Data (MB)"
                << "\t" << All_Write_Data.load() / 1024 / 1024 << endl;
        outfile << "All_Read_Data (MB)"
                << "\t" << All_Read_Data.load() / 1024 / 1024 << endl;

        outfile << "cache hit"
                << "\t" << Cache_Hit.load() << endl;
        outfile << "cache miss"
                << "\t" << Cache_Miss.load() << endl;
        outfile << "cache hit rate"
                << "\t" << (float)Cache_Hit.load() / (Cache_Hit.load() + Cache_Miss.load()) << endl;
        outfile << "cache size"
                << "\t" << metamod.StdRAID_meta->cache_mod.Cache_Root.size() << endl;

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
