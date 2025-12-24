#!/bin/bash

set -e
set -x

ROOT=/app/src

FILE=$1
FILE_NAME=${FILE::-2}

cd $ROOT
. ./setup.sh
./constantine -O0 $FILE -o $FILE_NAME.out || true 
llvm-dis $FILE_NAME.final.bc -o $FILE_NAME.final.ll

llc-13 -march=x86-64 -mattr=+avx512f,+avx512vl $FILE_NAME.final.bc
clang-13 -c $FILE_NAME.final.s -o $FILE_NAME.final.o
clang-13 -no-pie -o $FILE_NAME.out $FILE_NAME.final.o

echo "Success: $FILE_NAME.out"
