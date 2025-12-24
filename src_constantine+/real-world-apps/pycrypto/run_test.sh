#!/bin/bash 

set -e
set -x

ROOT=/app/src_constantine+
dir_name=$(basename "$PWD")

output_dir=`pwd`/output
mkdir -p $output_dir
output_file=$output_dir/run_test.log
[ -e $output_file ] && mv -f $output_file $output_file.bk

copy_file() {
    local src=$1
    local dst=$2
    mkdir -p $(dirname $dst)
    cp $src $dst
}

run_noperf() {
    simout=sim.txt
    echo "run_noperf: $1" >&2
    start_time=$(date +%s%N)
    for repeat in {1..10}; do
    ./$1 <random_input.txt >$simout
    done
    end_time=$(date +%s%N)
    elapsed_ns=$((end_time - start_time))
    echo "$1: ${elapsed_ns} ns" >&2
}

compile_single() {
name=$1
stride_size=$2

# origexe1=src/$name".orig"
# noavxexe1=src/$name".noavx"
avx512exe1=src/$name".avx512"

echo -e "#define DFL_STRIDE (${stride_size}uL)" > $ROOT/include/conf.h

echo -e "#define DFL_STRIDE (${stride_size}uL)" > $ROOT/include/conf.h

cd $ROOT/real-world-apps/$dir_name 
if [ "$3" ]; then
    SKIP=1 ./compile.sh $name
else
    ./compile.sh $name
fi

for exec1 in $avx512exe1
do
    llc-13 -march=x86-64 -mattr=+avx512f,+avx512vl $exec1.bc
    clang-13 -c $exec1.s -o $exec1.o
    clang-13 -no-pie -fno-exceptions -o $exec1.out -ldl -lm -pthread $exec1.o

    copy_file $exec1.ll $output_dir/${exec1}-${stride_size}.ll
    copy_file $exec1.out $output_dir/${exec1}-${stride_size}.out
done
}

project_list=(AES ARC4 Blowfish CAST DES DES3)

for stride_size in 64 4; do 
    compile_single ${project_list[0]} $stride_size
    for ((i = 1; i < ${#project_list[@]}; i++)); do
        compile_single ${project_list[i]} $stride_size "skip recompile" 
    done

    for ((i = 0; i < ${#project_list[@]}; i++)); do
        PROJECT_NAME=src/${project_list[i]}
        echo "== Run $PROJECT_NAME with stride size $stride_size" >> $output_file
        run_noperf $PROJECT_NAME.orig-bk.out 2>> $output_file
        # run_noperf $PROJECT_NAME.orig.out 2>> $output_file
        # run_noperf $PROJECT_NAME.noavx.out 2>> $output_file
        run_noperf $PROJECT_NAME.avx512.out 2>> $output_file
        echo "" >> $output_file
    done
done
