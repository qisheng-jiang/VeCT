#!/bin/bash

# run . ./setup.sh to set up the environment variables first 

FILE=$1
FILE_NAME=${FILE::-2}

docker exec constantine bash -c \
"cd /root/constantine/src && \
. ./setup.sh && \
./constantine -O0 $FILE -o $FILE_NAME.out"

# -dfl-avx512=1 -dfl-avx2=1 in constantine 
# #define DFL_STRIDE (64uL) in dfl.c 

../retdec/bin/retdec-decompiler $FILE_NAME.out > /dev/null
