#!/bin/bash

insmod /share/ouichefs.ko && 
mount /share/test.img ~/ouichefs && 
cd ~/ouichefs && 
stat -f -c %T .
rm -rf test
mkdir test 
cd ..
