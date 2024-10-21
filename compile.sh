#!/bin/bash
ZSIMPATH=$(pwd)
PINPATH=$ZSIMPATH/lib/pin_2.14
NVMAINPATH=$ZSIMPATH/lib/nvmain
BOOST=$ZSIMPATH/lib/boost_1_59_0
#LIBCONFIGPATH=$ZSIMPATH/lib/libconfig
NUMCPUS=$(grep -c ^processor /proc/cpuinfo)

if [ "$1" = "all" ]
then
	echo "Compiling all ..."
	#export LIBCONFIGPATH
        #cd $LIBCONFIGPATH
        #./configure --prefix=$LIBCONFIGPATH && make install
        #cd -

        cd $ZSIMPATH/contrib/boost_1_59_0
        ./bootstrap./sh
        ./b2 --toolset=gcc-4.8 --clean-all -n
        ./b2 --toolset=gcc-4.8 --buildtype=complete --cxxflags=" -g -std=c++0x -fabi-version=2 -D_GLIBCXX_USE_CXX11_ABI=0" -j $NUMCPUS
        ./b2 --toolset=gcc-4.8 --buildtype=complete --prefix=$BOOST install
        cd -

        export PINPATH
        export NVMAINPATH
        export BOOST
        scons -j$NUMCPUS
else
	echo "Compiling only ZSim ..."
        export PINPATH
        export NVMAINPATH
        export BOOST
        scons -j$NUMCPUS
fi
