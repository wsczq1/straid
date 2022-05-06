#! /bin/bash

MDname="md5"
blocksize="64K" # The size of user I/O requests when testing MD
wthread=(1 2 4 8 16 32 64)


if [ ! -d "FlameGraph" ]; then
    if [ $osinfo = ${ubuntu_os} ]; then
        echo "Ubuntu"
        echo_and_run echo "sudo apt install  linux-tools-common -y" | bash -
        echo_and_run echo "sudo apt install git  -y" | bash -
        echo_and_run echo "git clone https://github.com/brendangregg/FlameGraph" | bash -
    elif [ $osinfo = ${centos_os} ]; then
        echo "Centos"
        echo_and_run echo "sudo yum install perf -y" | bash -
        echo_and_run echo "sudo yum install git -y" | bash -
        echo_and_run echo "git clone https://github.com/brendangregg/FlameGraph" | bash -
    else
        echo "unknow osinfo:$osinfo"
        exit -1
    fi
fi

for th in ${wthread[@]}; do
    echo ${th} > /sys/block/${MDname}/md/group_thread_cnt 

    nohup fio bottle.fio &
    sleep 6

    process="${MDname}_raid5"
    processid=$(ps -ef | grep ${process} | grep -v grep | awk '{print $2}')
    echo "process name is ${process}, tid is ${processid}"

    rm -rf perf.*
    sudo perf record -F 9999 --call-graph dwarf -p ${processid} -- sleep 5
    sleep 1
    sudo perf script -i perf.data > perf.unfold
    ./FlameGraph/stackcollapse-perf.pl perf.unfold > perf.folded
    ./FlameGraph/flamegraph.pl perf.folded > perf-${th}.svg
    echo "flame svg file generated: perf-${th}.svg"

    fioid=$(ps -ef | grep fio | grep -v grep | awk '{print $2}')
    pkill -9 fio

    sleep 2
done

rm -rf nohup.out
rm -rf perf.*
echo 64 > /sys/block/${MDname}/md/group_thread_cnt 
