#!/bin/bash 

# set -e
# set -x

docker_name=constantine-org
FILE=test/vector-perf.c
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
    docker exec $docker_name bash -c \
    "cd /root/constantine/src && \
    . ./setup.sh && \
    ./constantine -O0 $FILE -o $FILE_NAME.out && \
    llvm-dis $FILE_NAME.final.bc -o $output_dir/$test_type.$array_size.$update_size.$is_load.final.ll"
    llc -march=x86-64 -mattr=+avx512f,+avx512vl $FILE_NAME.final.bc
    clang -c $FILE_NAME.final.s -o $FILE_NAME.final.o
    clang -no-pie -o $FILE_NAME.out $FILE_NAME.final.o

    for repeat in {1..10}
    do
    $FILE_NAME.out <./apps/binsec/random_input.txt 2>> $output_file
    done
    
    sleep 0.1

}



function run_insecure {
    local test_type=$1
    local array_size=$2
    local update_size=$3
    local is_load=$4
    local output_file=$5

    origexe1=$FILE_NAME".orig.out"
    bc1=$FILE_NAME".linked.bc"

    echo -e "#define TEST_TYPE $test_type\n#define ARRAY_SIZE $array_size\n#define UPDATE_SIZE $update_size\n#define IS_LOAD $is_load" > $FILE_NAME.h
    rm -f $FILE_NAME.out $FILE_NAME.base.bc
    rm -f $FILE_NAME.base.ll
    rm -f $origexe1 $bc1

    docker exec $docker_name bash -c \
    "cd /root/constantine/src && \
    . ./setup.sh && \
    bash test/build_orig.sh $FILE_NAME.c clang "" """

    echo "Running test with original-bk array_size=$array_size and update_size=$update_size for $test_type with is_load=$is_load" >> $output_file

    for repeat in {1..10}
    do
    $origexe1 <./apps/binsec/random_input.txt 2>> $output_file
    done

    sleep 0.1

}

echo -e "#define DFL_STRIDE (${stride_size}uL)" > ../include/conf.h

for stride_size in 64 4
do 
con_output_file=$output_dir/constantine-$stride_size.log
con_results_file=$output_dir/constantine-$stride_size.res
[ -e $con_output_file ] && mv -f $con_output_file $con_output_file.bk
[ -e $con_results_file ] && mv -f $con_results_file $con_results_file.bk

insecure_output_file=$output_dir/insecure-$stride_size.log
insecure_results_file=$output_dir/insecure-$stride_size.res
[ -e $insecure_output_file ] && mv -f $insecure_output_file $insecure_output_file.bk
[ -e $insecure_results_file ] && mv -f $insecure_results_file $insecure_results_file.bk


docker restart $docker_name

docker exec $docker_name bash -c \
"cd /root/constantine/src && \
. ./setup.sh && \
cd lib && rm ./dfl/dfl.o && make install -j10 && \
cd /root/constantine/src && \
cd passes && make install -j10" 


    update_size=6
    for test_type in uint32_t uint64_t
    do 
    for is_load in 0 1
    do 
        for array_size in 10 100 1000 10000 
        do 
            run_test $test_type $array_size $update_size $is_load $con_output_file
            run_insecure $test_type $array_size $update_size $is_load $insecure_output_file
        done
    done
    done


    array_size=1000

    test_type=uint32_t 
    for is_load in 0 1
    do 
        for update_size in 2 4 8 10 12 14 15 
        do 
            run_test $test_type $array_size $update_size $is_load $con_output_file
            run_insecure $test_type $array_size $update_size $is_load $insecure_output_file
        done
    done
    test_type=uint64_t 
    for is_load in 0 1
    do 
        for update_size in 2 4 7 
        do 
            run_test $test_type $array_size $update_size $is_load $con_output_file
            run_insecure $test_type $array_size $update_size $is_load $insecure_output_file
        done
    done


    python3 test/stats.py $con_output_file > $con_results_file
    echo "Output log saved to $con_output_file; Results saved to $con_results_file"

    python3 test/stats.py $insecure_output_file > $insecure_results_file
    echo "Output log saved to $insecure_output_file; Results saved to $insecure_results_file"

done 