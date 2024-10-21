#!/bin/bash
source ./env.sh
#echo 2147483648000 > /proc/sys/kernel/shmmax
sysctl -w kernel.shmmax=2147483648000
sysctl -w kernel.yama.ptrace_scope=0

./bin/zsim tests/simple.cfg
