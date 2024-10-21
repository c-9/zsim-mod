#!/bin/bash
source ./env.sh

NUMCPUS=$(grep -c ^processor /proc/cpuinfo)

if [ "$1" = "all" ]
then
	echo "Cleaning all ..."
	#export LIBCONFIGPATH
	#cd $LIBCONFIGPATH
	#make clean-libtool
	#cd -
	scons -c
	rm -rf build/ bin/
else
	echo "Cleaning Zsim ..."
	scons -c
	rm -rf build/ bin/
fi
