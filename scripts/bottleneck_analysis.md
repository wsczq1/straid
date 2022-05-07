# Bottleneck analysis
In this experiment, we evaluate the write performance of Linux MD RAID 5 at different threads number and analyze the write latency breakdown by Linux-perf tool.

## Install submodules
In the sections of motivation and evaluation in our paper, we used some tools for analysis and comparison, so we introduced them as submodules. To execute these parts of the code, please install the submodules.
```
git submodule update --init --recursive
```

## Tools and Dependences
In the sections of motivation in our paper, we used some tools for analysis and comparison.  

1. Verify that fio tools `fio` has been installed.
```
apt update
apt upgrade
apt install fio
```

2. Verify that multiple devices admin `mdadm` has been installed.  
```
apt install mdadm
```

3. If you cannot use the `perf` command, install the Linux-perf tool
```
sudo apt-get install linux-tools-common
sudo apt-get install linux-tools-"$(uname -r)"
sudo apt-get install linux-cloud-tools-"$(uname -r)"
sudo apt-get install linux-tools-generic
sudo apt-get install linux-cloud-tools-generic
```

4. For data visualization, we use the `FlameGraph` tool to show the latency breakdown. FlameGraph is already available in this repository.

5. `Python3` is needed to run the scripts.

5. `numactl` is needed to bind the I/O threads to a dedicated CPU socket.
```
apt install numactl
```

## Configure Linux MD RAID
Linux multiple disk (MD) module has been integrated into the Linux Kernel.
   ```
   cd scripts
   vim md_config.sh
    # Modify RAID build configures in scripts

   ./md_config.sh
    # Waiting for MD build to complete
   ```

## Run MD performance test
1. Assign the MD directory in the script
```
sudo chmod 777 -R ./scripts

cd scripts
vim raid5.fio

# directory="your MD directory"
```

2. Configure and Run test script
```
vim MD_perftest.sh

# I/O request size
blocksize=64K 
# Number of running threads  
thread=(1 2 4 8 16 32 64)

./MD_perftest.sh
```   

3. Run perf analyze script and see throughputs and latencies results
```
python3 md_analyze.py
```   


## Run MD bottleneck test

1. Config testing script
```
cd scripts
vim bottleneck_perf.sh

# The name of MD device
MDname="md5"
# The size of user I/O requests when testing MD bottleneck
blocksize="64K" 

```

2. Run test script
```
./bottleneck_perf.sh
```

3. See flame graphs of latency breakdown in `perf-[#threads].svg`.

