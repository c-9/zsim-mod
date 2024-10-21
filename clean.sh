#!/bin/bash
ZSIMPATH=$(pwd)
PINPATH=$ZSIMPATH/lib/pin_2.14
NVMAINPATH=$ZSIMPATH/lib/nvmain
BOOST=$ZSIMPATH/lib/boost_1_59_0
#LIBCONFIGPATH=$ZSIMPATH/lib/libconfig
NUMCPUS=$(grep -c ^processor /proc/cpuinfo)

if [ "$1" = "all" ]
then
	echo "Cleaning all ..."
	#export LIBCONFIGPATH
	#cd $LIBCONFIGPATH
	#make clean-libtool
	#cd -
	export PINPATH
	export NVMAINPATH
	export BOOST
	scons -c
	rm -rf build/ bin/
else
	echo "Cleaning Zsim ..."
	export PINPATH
	export NVMAINPATH
	export BOOST
	scons -c
	rm -rf build/ bin/
fi
