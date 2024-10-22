#!/bin/bash
source ./env.sh
#echo 18446744073692774399 > /proc/sys/kernel/shmmax
sysctl -w kernel.shmmax=18446744073692774399
sysctl -w kernel.yama.ptrace_scope=0

./bin/zsim tests/simple.cfg
