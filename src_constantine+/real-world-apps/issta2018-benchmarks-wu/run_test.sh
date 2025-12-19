#!/bin/bash 

# set -e
# set -x

docker_name=constantine-org
dir_name=$(basename "$PWD")

output_dir=output
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

origexe1=examples/$name".orig"
noavxexe1=examples/$name".noavx"
avx512exe1=examples/$name".avx512"

echo -e "#define DFL_STRIDE (${stride_size}uL)" > ../../include/conf.h

if [ "$3" ]; then
    docker exec $docker_name bash -c \
    "cd /root/constantine/src/apps/$dir_name && \
    SKIP=1 ./compile.sh $name"
else
    docker exec $docker_name bash -c \
    "cd /root/constantine/src/apps/$dir_name && \
    ./compile.sh $name"
fi

for exec1 in $origexe1 $noavxexe1 $avx512exe1
do
    llc -march=x86-64 -mattr=+avx512f,+avx512vl $exec1.bc
    clang -c $exec1.s -o $exec1.o
    clang -no-pie -fno-exceptions -o $exec1.out -ldl -lm -pthread $exec1.o

    copy_file $exec1.ll $output_dir/${exec1}-${stride_size}.ll
    copy_file $exec1.out $output_dir/${exec1}-${stride_size}.out
done
}

project_list=(
    appliedCryp/des 
    chronos/aes   chronos/des   chronos/des3  chronos/anubis  chronos/cast5  chronos/cast6  chronos/fcrypt chronos/khazad 
    ghostrider/dijkstra ghostrider/binsearch ghostrider/histogram  
    ghostrider/rsort 
    ghostrider/permutation 
    ghostrider/heappop
    libg/camellia libg/des  libg/seed  libg/twofish
    supercop/aes_core  supercop/cast-ssl
    )


for stride_size in 64 4; do 
    compile_single ${project_list[0]} $stride_size
    for ((i = 1; i < ${#project_list[@]}; i++)); do
        compile_single ${project_list[i]} $stride_size "skip recompile" 
    done

    for ((i = 0; i < ${#project_list[@]}; i++)); do
        PROJECT_NAME=examples/${project_list[i]}
        echo "== Run $PROJECT_NAME with stride size $stride_size" >> $output_file
        run_noperf $PROJECT_NAME.orig-bk.out 2>> $output_file
        run_noperf $PROJECT_NAME.orig.out 2>> $output_file
        run_noperf $PROJECT_NAME.noavx.out 2>> $output_file
        run_noperf $PROJECT_NAME.avx512.out 2>> $output_file
        echo "" >> $output_file
    done
done
