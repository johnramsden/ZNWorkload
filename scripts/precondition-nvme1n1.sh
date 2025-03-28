#!/bin/sh

set -e

sgdisk --zap-all /dev/nvme1n1

fio --name=precondition --filename=/dev/nvme1n1 --direct=1 \
    --rw=randwrite --bs=64k --size=100% --loops=1 \
    --randrepeat=0 --ioengine=libaio \
    --numjobs=1 --group_reporting


sgdisk -n 1:2048B:107701MiB -n 2:0:0 /dev/nvme1n1

