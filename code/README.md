This repository exposes part of the test scripts, related codes and public workload traces for running core tests on the StRAID prototype.

## Hardware Dependencies
We recommend deploying at least four dedicated NVMe SSDs for building StRAID in the experimental platform. These SSDs should to be installed on the PCIe slots belonging to a same CPU socket. Please note that the use of hardware and the installation method might produce different experimental results.


## Software Dependencies

Run the script to install dependencies
```
 cd code
 sh ./install_depends.sh 
```

or install dependencies as follows
   ```
   sudo apt update
   sudo apt upgrade
   sudo apt install cmake
   sudo apt install build-essential
   sudo apt install autoconf
   sudo apt install yasm
   sudo apt install nasm
   sudo apt install libtool-bin
   sudo apt install libtbb-dev
   sudo apt install libcds-dev
   ```

### Intel isa-l
https://github.com/intel/isa-l
   ```
    cd include/isa-l
    ./autogen.sh
    ./configure
    make
    sudo make install
   ```

### IO_uring
https://github.com/axboe/liburing

   ```
    cd include/liburing
    ./configure
    make
    make install
   ```

### libcuckoo
https://github.com/efficient/libcuckoo
   ```
    cd include/libcuckoo
    mkdir build
    cd build
    cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_EXAMPLES=1 -DBUILD_TESTS=1 ..
    make all
    make install
   ```


# Evaluation
We made scripts for two main test cases, including basic write/read tests and a trace-driven test. The first tests generate fixed numbers of requests with various I/O sizes and access patterns on StRAID. The trace-driven test will replay block-level I/O traces recorded in the trace file. 

Please note that StRAID is currently a prototype and may crash unexpectedly. If you experience a crash or get stuck, please repeat the experimental program or script several times. If the test program keeps failing, please check the system configuration and input parameters are correct.

# Micro-benchmarks

1. Set environment
   
   The environment variable like the number of instances (NUM_THREADS) and the size of dataset (DATASET_SIZE) should be set at the compile time, detailed are shown as follows:

   ```
   vim ./include/define.h

   // The number of running I/O-issuing Threads (IOT) and Workering Threads (WT)
   #define NUM_THREADS (16) 
   
   // The dataset size and the number of repeating workload executions
   #define DATASET_SIZE (1 * GB)
   #define LOOP (1)    

   // The I/O size of each read or write requests
   #define IO_SIZE PARTSIZE // IO size for partial-stripe write
   #define IO_SIZE FULLSIZE // IO size for full-stripe write

   // Number of data chunks and parity chunks in a stripe, which have to be consist with the number of deployed SSD devices. For example, a 5+1 RAID-5 should set the DATACHUNK_NUM to 5 and the PARITYCHUNK_NUM to 1
   #define DATACHUNK_NUM (5)                              
   #define PARITYCHUNK_NUM (1)                    
   ```

   Besides, the path of SSD devices and/or MD RAID should be assigned to the program.

   ```
   vim ./include/main.cc
   vim ./include/bench.cc

   // Assign paths of SSD devices used in experiment
   string lfile0 = "/dev/nvme0n1";
   string lfile1 = "/dev/nvme1n1";
   string lfile2 = "/dev/nvme2n1";
   string lfile3 = "/dev/nvme3n1";
   string lfile4 = "/dev/nvme4n1";
   string lfile5 = "/dev/nvme5n1";

   ```

2. Compile StRAID prototype test program in the default settings.
   ```
   cd code
   make -j 4
   ```
3. Config and run micro-benchmark test script
   
   This script first compiles StRAID based on the benchmark conditions assigned by user, and then launches test with four types of workload patterns (i.e., sequential/random Read and sequential/random Write).
   ```
   vim ./run_bench.sh

   # set this array to modify the number of launched I/O threads
   thread=(1 2 4 8 16 32 64) 

   # set this variable to modify the chunk size in StRAID to (chunksize * KB)
   chunksize=64 

   # set the variable to test full-stripe (FULLSIZE) or partial-stripe (PARTSIZE) write
   IOsize="FULLSIZE" 
   ```

   ```
   ./run_bench.sh
   ```
4. The throughput and latency results are shown in:

   ```
   ./results/bench_out.txt
   ```
   For each benchmark pattern, the program will output its throughput and latency performance separately, including: 
   
   + *Average Throughput*
   + *IOPS*
   + *Average Latency* 
   + *50th-percentile Latency* 
   + *90th-percentile Latency* 
   + *99th-percentile Latency* 
   + *999th-percentile Latency*



# Macro-benchmarks

This repository exposes one typical block traces from `Filebench` for testing Linux MD and StRAID.

1. Set environment
   ```
   vim ./run_tracemd.sh

   thread=8          # set the number of user threads
   mdtype="5"        # set RAID level
   mdpath="/dev/md5" # set RAID path
   ```

   ```
   vim ./run_tracest.sh

   thread=8    # set the number of user threads
   chunksize=8 # set the chunk size of StRAID to (chunksize * KB)
   ```

2. Compile test program
   ```
   make -j 4
   ```

3. Launch marco-benchmark
   ```
   # marco-benchmark on Linux MD
   ./run_tracemd.sh

   # marco-benchmark on StRAID
   ./run_tracest.sh
   ```

4. The throughput and latency results are shown in:
   ```
   # results of Linux MD
   ./results/MD_trace_results.txt

   # results of StRAID
   ./results/ST_trace_results.txt
   ```

   The results contain: 
   + *Total throughput per second*
   + *Write throughput per second* 
   + *IO per second (IOPS)*
   + *Latency CDF*