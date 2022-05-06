#!/usr/bin/python3
# -*- coding: utf-8 -*-

import datetime
import time
import os
import sys
import re

if __name__ == "__main__":

    if len(sys.argv) > 1:
        input_path = './' + sys.argv[1]
    else:
        input_path = './fio_results.log'

    try:
        input_file = open(input_path)
    except IOError:
        print('File Not Exist')

    pattern_write = 'write:'
    pattern_read = 'read:'
    pattern_end = 'Disk stats'

    iops_list = []
    bandwidth_list = []
    slatency_list = []
    clatency_list = []

    lat50_list = []
    lat90_list = []
    lat95_list = []
    lat99_list = []
    lat999_list = []
    lat9999_list = []

    time_flag = 0

    for line in input_file.readlines():
        line = line.replace("= ", "=")
        # print(line)

        iops = 0
        bandwidth = 0
        slat = 0        # 
        clat = 0        # 
        lat50 = 0
        lat90 = 0
        lat95 = 0
        lat99 = 0
        lat999 = 0
        lat9999 = 0

        if (re.search(pattern_write, line) != "None") or (re.search(pattern_read, line) != "None"):
            split_line = (line.strip()).split(" ")
            for temp in split_line:
                if re.search("IOPS=", temp):
                    if re.search("M", temp):
                        iops = float(re.findall(r"\d+\.?\d*", temp)
                                     [0]) * 1000 * 1000
                    elif re.search("k", temp):
                        iops = float(re.findall(r"\d+\.?\d*", temp)[0]) * 1000
                    else:
                        iops = float(re.findall(r"\d+\.?\d*", temp)[0])
                    round(iops, 3)
                    iops_list.append(iops)
                if re.search("BW=", temp):
                    if re.search("KiB", temp):
                        bandwidth = float(re.findall(
                            r"\d+\.?\d*", temp)[0]) * 1.024 / 1024
                    elif re.search("MiB", temp):
                        bandwidth = float(re.findall(
                            r"\d+\.?\d*", temp)[0]) * 1.048
                    elif re.search("GiB", temp):
                        bandwidth = float(re.findall(
                            r"\d+\.?\d*", temp)[0]) * 1.073 * 1024
                    round(bandwidth, 3)
                    bandwidth_list.append(bandwidth)
                # if 'slat' in split_line and re.search("avg=",temp):
                #     slat = float(re.findall(r"\d+\.?\d*",temp)[0])
                #     slatency_list.append(slat)
                if 'clat' in split_line and re.search("avg=", temp) and len(re.findall(r"\d+\.?\d*", temp)) >= 1:
                    # print(line)
                    if '(nsec)' in line:
                        clat = float(re.findall(r"\d+\.?\d*", temp)[0])
                        # if clat == 0:
                        #     clat = clat = float(re.findall(r"\d+\.?\d*",temp[1:])[0])
                    elif '(usec)' in line:
                        clat = float(re.findall(r"\d+\.?\d*", temp)[0]) * 1000
                        # if clat == 0:
                        #     clat = clat = float(re.findall(r"\d+\.?\d*",temp[1:])[0]) * 1000
                    elif '(msec)' in line:
                        clat = float(re.findall(r"\d+\.?\d*", temp)[0]) * 1000 * 1000
                    clatency_list.append(clat)

            if 'clat percentiles' in line:
                # print(line)
                if '(nsec)' in line:
                    time_flag = 0
                elif '(usec)' in line:
                    time_flag = 1
                elif '(msec)' in line:
                    time_flag = 2
            
            split_line = (line.strip()).split(",")
            for temp in split_line:

                if '50.00th' in temp:
                    lat50 = int(re.search(r"(?<=\[).*?(?=\])", temp)[0])
                    if time_flag == 0:
                        lat50 = lat50
                    if time_flag == 1:
                        lat50 = lat50 * 1000
                    if time_flag == 2:
                        lat50 = lat50 * 1000 * 1000
                    lat50_list.append(lat50)

                if '90.00th' in temp:
                    lat90 = int(re.search(r"(?<=\[).*?(?=\])", temp)[0])
                    if time_flag == 0:
                        lat90 = lat90
                    if time_flag == 1:
                        lat90 = lat90 * 1000
                    if time_flag == 2:
                        lat90 = lat90 * 1000 * 1000
                    lat90_list.append(lat90)
                        
                if '99.00th' in temp:
                    lat99 = int(re.search(r"(?<=\[).*?(?=\])", temp)[0])
                    if time_flag == 0:
                        lat99 = lat99
                    if time_flag == 1:
                        lat99 = lat99 * 1000
                    if time_flag == 2:
                        lat99 = lat99 * 1000 * 1000
                    lat99_list.append(lat99)

                if '99.90th' in temp:
                    lat999 = int(re.search(r"(?<=\[).*?(?=\])", temp)[0])
                    if time_flag == 0:
                        lat999 = lat999
                    if time_flag == 1:
                        lat999 = lat999 * 1000
                    if time_flag == 2:
                        lat999 = lat999 * 1000 * 1000
                    lat999_list.append(lat999)

                
                    # print("Bandwidth(MB/s)" + '\t' + 'IOPS' + '\t' + 'Latency')
                    # for i in range(min(len(iops_list),len(bandwidth_list),len(clatency_list))):
                    #     print(str(bandwidth_list[i]) + '\t' + str(iops_list[i]) + '\t' + str(clatency_list[i]))
                    # print()

    print('For Copy')
    print("Bandwidth\n")
    for i in range(0, len(bandwidth_list), 4):                # Bandwidth
        for j in range(i, i+4):
            print(str(bandwidth_list[j]) + '\t', end='')
        print()
    print('\n')

    print("IOPS\n")
    for i in range(0, len(iops_list), 4):                     # IOPS
        for j in range(i, i+4):
            print(str(iops_list[j]) + '\t', end='')
        print()
    print('\n')

    print("Avg. Latency\n")
    for i in range(0, len(clatency_list), 4):                 # Avg. Latency
        for j in range(i, i+4):
            print(str(clatency_list[j]) + '\t', end='')
        print()
    print('\n')

    print("50th\n")
    for i in range(0, len(lat50_list), 4):                 # 999th
        for j in range(i, i+4):
            print(str(lat50_list[j]) + '\t', end='')
        print()
    print('\n')

    print("90th\n")
    for i in range(0, len(lat90_list), 4):                # 90th
        for j in range(i, i+4):
            print(str(lat90_list[j]) + '\t', end='')
        print()
    print('\n')

    print("99th\n")
    for i in range(0, len(lat99_list), 4):                     # 99th
        for j in range(i, i+4):
            print(str(lat99_list[j]) + '\t', end='')
        print()
    print('\n')

    print("999th\n")
    for i in range(0, len(lat999_list), 4):                 # 999th
        for j in range(i, i+4):
            print(str(lat999_list[j]) + '\t', end='')
        print()
    print('\n')


