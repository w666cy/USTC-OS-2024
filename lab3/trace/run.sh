#! /bin/bash

FITMODE=$1
if [ $FITMODE -eq 1 ]
then
    printf "\nTest first-fit allocation\n\n"
elif [ $FITMODE -eq 0 ]
then
    printf "\nTest best-fit allocation\n\n"
else
    printf "Wrong argument!\n"
    printf "Type command \"sh ./run.sh 0\" to run best-fit allocator.\n"
    printf "Or type command \"sh ./run.sh 1\" to run first-fit allocator.\n"
    exit
fi

TRACEPATH="$PWD/`dirname $0`"
MALLOCPATH="$TRACEPATH/../malloclab/"
export LD_LIBRARY_PATH=$MALLOCPATH:$LD_LIBRARY_PATH
cd $MALLOCPATH; make clean; make FIRST_FIT=$FITMODE
cd $TRACEPATH
g++ workload.cc -o workload -I$MALLOCPATH -L$MALLOCPATH -lmem -lpthread -std=c++11
./workload