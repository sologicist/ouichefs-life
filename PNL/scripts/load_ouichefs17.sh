#!/bin/bash

path="/Documents/M1/PNL/projet/kernel"

cp files/file17.c ../file.c
cd ..
make KERNELDIR=/tmp/pnl_2023-2024/linux-6.5.7 && cp ouichefs.ko $path/share
cp $path/test.img $path/share/test.img
cd scripts
./qemu.sh
