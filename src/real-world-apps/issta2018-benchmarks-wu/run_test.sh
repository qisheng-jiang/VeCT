#!/bin/bash 

# set -e
# set -x

docker_name=constantine
dir_name=$(basename "$PWD")

output_dir=output
mkdir -p $output_dir
output_file=$output_dir/run_test.log
[ -e $output_file ] && mv -f $output_file $output_file.bk
stats_file=$output_file.stats.csv
[ -e $stats_file ] && mv -f $stats_file $stats_file.bk
echo "filename,num_vec_reads,vectorized_reads,wrapped_reads,linearized_reads,total_reads,num_vec_writes,vectorized_writes,wrapped_writes,linearized_writes,total_writes,output_reads,output_writes" > $stats_file

copy_file() {
    local src=$1
    local dst=$2
    mkdir -p $(dirname $dst)
    cp $src $dst
}

read_stats() {
    total_reads=$(grep -m 1 "\[+\] Total Reads:" stats.txt | grep -o -E '[0-9]+' || echo '0')
    total_writes=$(grep -m 1 "\[+\] Total Writes:" stats.txt | grep -o -E '[0-9]+' || echo '0')
    linearized_reads=$(grep -m 1 "\[+\] Linearized Reads:" stats.txt | grep -o -E '[0-9]+' || echo '0')
    linearized_writes=$(grep -m 1 "\[+\] Linearized Writes:" stats.txt | grep -o -E '[0-9]+' || echo '0')
    wrapped_reads=$(grep -m 1 "\[+\] Wrapped Reads:" stats.txt | grep -o -E '[0-9]+' || echo '0')
    wrapped_writes=$(grep -m 1 "\[+\] Wrapped Writes:" stats.txt | grep -o -E '[0-9]+' || echo '0')
	vectorized_reads=$(grep -m 1 "\[+\] Vectorized Reads:" stats.txt | grep -o -E '[0-9]+' || echo '0')
	vectorized_writes=$(grep -m 1 "\[+\] Vectorized Writes:" stats.txt | grep -o -E '[0-9]+' || echo '0')
    num_vec_reads=$(grep -m 1 "\[+\] Num Vector Reads:" stats.txt | grep -o -E '[0-9]+' || echo '0')
    num_vec_writes=$(grep -m 1 "\[+\] Num Vector Writes:" stats.txt | grep -o -E '[0-9]+' || echo '0')

	echo "$1,$num_vec_reads,$vectorized_reads,$wrapped_reads,$linearized_reads,$total_reads,$num_vec_writes,$vectorized_writes,$wrapped_writes,$linearized_writes,$total_writes,$vectorized_reads/$((vectorized_reads + wrapped_reads))/$total_reads,$vectorized_writes/$((vectorized_writes + wrapped_writes))/$total_writes"
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
read_only=$3
skip_compile=$4

noavxexe1=examples/$name".noavx"
avx512exe1=examples/$name".avx512"

echo -e "#define DFL_STRIDE (${stride_size}uL)\n#define DFL_VECTORIZE (false)\n#define DFL_READONLY ($read_only)" > ../../include/conf.h

if [ "$skip_compile" ]; then
    docker exec $docker_name bash -c \
    "cd /root/constantine/src/apps/$dir_name && \
    SKIP=1 ./compile.sh $name"
else
    docker exec $docker_name bash -c \
    "cd /root/constantine/src/apps/$dir_name && \
    ./compile.sh $name"
fi

for exec1 in $noavxexe1 $avx512exe1
do
    llc -march=x86-64 -mattr=+avx512f,+avx512vl $exec1.bc
    clang -c $exec1.s -o $exec1.o
    clang -no-pie -fno-exceptions -o $exec1.out -ldl -lm -pthread $exec1.o

    copy_file $exec1.ll $output_dir/${exec1}-${stride_size}.ll
    copy_file $exec1.out $output_dir/${exec1}-${stride_size}.out
done
}

compile_vector() {
name=$1
stride_size=$2
read_only=$3
pre_cfl=$4
skip_compile=$5

avx512vectorexe1=examples/$name".avx512.vector"

echo -e "#define DFL_STRIDE (${stride_size}uL)\n#define DFL_VECTORIZE (true)\n#define DFL_READONLY ($read_only)" > ../../include/conf.h

if [ "$skip_compile" ]; then
    docker exec $docker_name bash -c \
    "cd /root/constantine/src/apps/$dir_name && \
    PRE_CFL=$pre_cfl SKIP=1 VECTOR=1 ./compile.sh $name"
else
    docker exec $docker_name bash -c \
    "cd /root/constantine/src/apps/$dir_name && \
    PRE_CFL=$pre_cfl VECTOR=1 ./compile.sh $name"
fi

llc -march=x86-64 -mattr=+avx512f,+avx512vl $avx512vectorexe1.bc
clang -c $avx512vectorexe1.s -o $avx512vectorexe1.o
clang -no-pie -fno-exceptions -o $avx512vectorexe1.out -ldl -lm -pthread $avx512vectorexe1.o

copy_file $avx512vectorexe1.ll $output_dir/${avx512vectorexe1}-${stride_size}-${pre_cfl}.ll
copy_file $avx512vectorexe1.out $output_dir/${avx512vectorexe1}-${stride_size}-${pre_cfl}.out
copy_file stats.txt $output_dir/${avx512vectorexe1}-${stride_size}-${pre_cfl}.stats.txt
read_stats "$avx512vectorexe1-${stride_size}-${pre_cfl}" >> $stats_file
}


# ======================
# READ WRITE + NO PRE CFL 
# ======================

project_list=(
    appliedCryp/des 
    ghostrider/dijkstra ghostrider/histogram 
    ghostrider/rsort 
    ghostrider/permutation 
    ghostrider/heappop
    )

for stride_size in 64 4
do 
    compile_single ${project_list[0]} $stride_size 0
    for ((i = 1; i < ${#project_list[@]}; i++)); do
        compile_single ${project_list[i]} $stride_size 0 "skip recompile" 
    done

    compile_vector ${project_list[0]} $stride_size 0 0
    for ((i = 1; i < ${#project_list[@]}; i++)); do
        compile_vector ${project_list[i]} $stride_size 0 0 "skip recompile" 
    done

    for ((i = 0; i < ${#project_list[@]}; i++)); do
        PROJECT_NAME=examples/${project_list[i]}
        echo "== Run $PROJECT_NAME with stride size $stride_size" >> $output_file
        run_noperf $PROJECT_NAME.noavx.out 2>> $output_file
        run_noperf $PROJECT_NAME.avx512.out 2>> $output_file
        run_noperf $PROJECT_NAME.avx512.vector.out 2>> $output_file
        echo "" >> $output_file
    done
done


# ======================
# READ WRITE + PRE CFL 
# ======================

project_list=(
    appliedCryp/des 
    ghostrider/dijkstra ghostrider/histogram 
    ghostrider/rsort 
    ghostrider/permutation 
    ghostrider/heappop
    )

for stride_size in 64 4
do 
    compile_vector ${project_list[0]} $stride_size 0 1
    for ((i = 1; i < ${#project_list[@]}; i++)); do
        compile_vector ${project_list[i]} $stride_size 0 1 "skip recompile" 
    done

    for ((i = 0; i < ${#project_list[@]}; i++)); do
        PROJECT_NAME=examples/${project_list[i]}
        echo "== Run pre_cfl/$PROJECT_NAME with stride size $stride_size" >> $output_file
        run_noperf $PROJECT_NAME.avx512.vector.out 2>> $output_file
        echo "" >> $output_file
    done
done


# ======================
# READ ONLY + NO PRE CFL 
# ======================

project_list=(
    chronos/aes   chronos/des   chronos/des3  chronos/anubis  chronos/cast5  chronos/cast6  chronos/fcrypt chronos/khazad 
    ghostrider/binsearch 
    libg/camellia libg/des  libg/seed  libg/twofish
    supercop/aes_core  supercop/cast-ssl
    )

for stride_size in 64 4
do 
    compile_single ${project_list[0]} $stride_size 1
    for ((i = 1; i < ${#project_list[@]}; i++)); do
        compile_single ${project_list[i]} $stride_size 1 "skip recompile" 
    done

    compile_vector ${project_list[0]} $stride_size 1 0
    for ((i = 1; i < ${#project_list[@]}; i++)); do
        compile_vector ${project_list[i]} $stride_size 1 0 "skip recompile" 
    done

    for ((i = 0; i < ${#project_list[@]}; i++)); do
        PROJECT_NAME=examples/${project_list[i]}
        echo "== Run $PROJECT_NAME with stride size $stride_size" >> $output_file
        run_noperf $PROJECT_NAME.noavx.out 2>> $output_file
        run_noperf $PROJECT_NAME.avx512.out 2>> $output_file
        run_noperf $PROJECT_NAME.avx512.vector.out 2>> $output_file
        echo "" >> $output_file
    done
done


# ======================
# READ ONLY + PRE CFL 
# ======================

project_list=(
    chronos/aes   chronos/des   chronos/des3  chronos/anubis  chronos/cast5  chronos/cast6  chronos/fcrypt chronos/khazad 
    ghostrider/binsearch 
    libg/camellia libg/des  libg/seed  libg/twofish
    supercop/aes_core  supercop/cast-ssl
    )

for stride_size in 64 4
do 
    compile_vector ${project_list[0]} $stride_size 1 1
    for ((i = 1; i < ${#project_list[@]}; i++)); do
        compile_vector ${project_list[i]} $stride_size 1 1 "skip recompile" 
    done

    for ((i = 0; i < ${#project_list[@]}; i++)); do
        PROJECT_NAME=examples/${project_list[i]}
        echo "== Run pre_cfl/$PROJECT_NAME with stride size $stride_size" >> $output_file
        run_noperf $PROJECT_NAME.avx512.vector.out 2>> $output_file
        echo "" >> $output_file
    done
done
