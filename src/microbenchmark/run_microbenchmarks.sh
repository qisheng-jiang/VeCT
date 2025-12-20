#!/bin/bash 

set -e
set -x

ROOT=/app/src
FILE=`pwd`/vector-perf.c
FILE_NAME=${FILE::-2}

output_dir=$FILE_NAME-output
mkdir -p $output_dir

function run_test {
    local test_type=$1
    local array_size=$2
    local update_size=$3
    local is_load=$4
    local output_file=$5

    echo -e "#define TEST_TYPE $test_type\n#define ARRAY_SIZE $array_size\n#define UPDATE_SIZE $update_size\n#define IS_LOAD $is_load" > $FILE_NAME.h
    rm -f $FILE_NAME.out $FILE_NAME.base.bc $FILE_NAME.dfl.bc $FILE_NAME.final.bc
    rm -f $FILE_NAME.base.ll $FILE_NAME.dfl.ll $FILE_NAME.final.ll
    rm -f $FILE_NAME.final.s $FILE_NAME.final.o

    echo "Running test with array_size=$array_size and update_size=$update_size for $test_type with is_load=$is_load" >> $output_file
    cd $ROOT
    . ./setup.sh 
    ./constantine -O0 $FILE -o $FILE_NAME.out || true 
    llvm-dis $FILE_NAME.final.bc -o $output_dir/$test_type.$array_size.$update_size.$is_load.final.ll
    llc-13 -march=x86-64 -mattr=+avx512f,+avx512vl $FILE_NAME.final.bc
    clang-13 -c $FILE_NAME.final.s -o $FILE_NAME.final.o
    clang-13 -no-pie -o $FILE_NAME.out $FILE_NAME.final.o

    for repeat in {1..10}
    do
    $FILE_NAME.out <$ROOT/real-world-apps/binsec/random_input.txt 2>> $output_file
    done
    
    sleep 0.1

}

for vectorize in "false" "true"
do 
for stride_size in 64 4
do 

echo -e "#define DFL_STRIDE (${stride_size}uL)\n#define DFL_VECTORIZE (${vectorize})\n#define DFL_READONLY (0)" > $ROOT/include/conf.h

output_file=$output_dir/$vectorize-$stride_size.log
results_file=$output_dir/$vectorize-$stride_size.res

[ -e $output_file ] && mv -f $output_file $output_file.bk
[ -e $results_file ] && mv -f $results_file $results_file.bk

cd $ROOT
. ./setup.sh
cd lib 
rm -f ./dfl/dfl.o
make install -j10
cd $ROOT
cd passes
rm -f ./dfl/dfl.so ./dfl/dfl.o
make install -j10

    update_size=6
    for test_type in uint32_t uint64_t
    do 
    for is_load in 0 1
    do 
        for array_size in 10 100 1000 10000 
        do 
            run_test $test_type $array_size $update_size $is_load $output_file
        done
    done
    done


    array_size=1000

    test_type=uint32_t 
    for is_load in 0 1
    do 
        for update_size in 2 4 8 10 12 14 15 
        do 
            run_test $test_type $array_size $update_size $is_load $output_file
        done
    done
    test_type=uint64_t 
    for is_load in 0 1
    do 
        for update_size in 2 4 7 
        do 
            run_test $test_type $array_size $update_size $is_load $output_file
        done
    done

    python3 $ROOT/microbenchmark/stats.py $output_file > $results_file
    echo "Output log saved to $output_file; Results saved to $results_file"

done
done
