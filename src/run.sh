#!/bin/bash

# run . ./setup.sh to set up the environment variables first 

docker_name=constantine

docker exec $docker_name bash -c \
"cd /root/constantine/src && \
. ./setup.sh && \
rm -f ./bin/dfl.bcc && rm -f ./bin/cfl.bcc && \
cd lib && rm -f ./dfl/dfl.o && make install -j10 && \
cd /root/constantine/src && \
cd passes && rm -f ./dfl/dfl.so ./dfl/dfl.o && make install -j10" 

FILE=$1
FILE_NAME=${FILE::-2}

docker exec $docker_name bash -c \
"cd /root/constantine/src && \
. ./setup.sh && \
rm -f $FILE_NAME.out $FILE_NAME.base.bc $FILE_NAME.dfl.bc $FILE_NAME.final.bc && \
rm -f $FILE_NAME.base.ll $FILE_NAME.dfl.ll $FILE_NAME.final.ll && \
rm -f $FILE_NAME.final.s $FILE_NAME.final.o && \
./constantine -O0 $FILE -o $FILE_NAME.out && \
llvm-dis $FILE_NAME.base.bc -o $FILE_NAME.base.ll && \
llvm-dis $FILE_NAME.dfl.bc -o $FILE_NAME.dfl.ll && \
llvm-dis $FILE_NAME.final.bc -o $FILE_NAME.final.ll "


llc -march=x86-64 -mattr=+avx512f,+avx512vl $FILE_NAME.final.bc
clang -c $FILE_NAME.final.s -o $FILE_NAME.final.o
clang -no-pie -o $FILE_NAME.out $FILE_NAME.final.o

# -dfl-avx512=1 -dfl-avx2=1 in constantine 
# #define DFL_STRIDE (64uL) in dfl.c 

../retdec/bin/retdec-decompiler $FILE_NAME.out > /dev/null
