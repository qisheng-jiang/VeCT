#! /bin/bash

set -e
set -x

CPU_CORE=1
mkdir -p data

# ===================
# T-test for AVX512 packed load/store operations with varying masks
# ===================

output_file=data/packed_singleline_time.csv
csv_file=${output_file}"_dudect_results.csv"
[ -e $output_file ] && mv $output_file $output_file.bk
[ -e $csv_file ] && mv $csv_file $csv_file.bk

gcc -mavx2 -mavx512f -mavx512bw -mavx512vl -mavx512dq \
    -O0 -DVMOV=1 -o avx512_store.out \
    avx_test.c -lm 

echo "Running avx512_mask_load/store for flush"
taskset -c $CPU_CORE ./avx512_store.out > $output_file

python3 run_dudect.py -f $output_file 


# ===================
# T-test for AVX512 gather/scatter operations with varying masks
# ===================

output_file=data/gather_scatter_singleline_time.csv
csv_file=${output_file}"_dudect_results.csv"
[ -e $output_file ] && mv $output_file $output_file.bk
[ -e $csv_file ] && mv $csv_file $csv_file.bk

gcc -mavx2 -mavx512f -mavx512bw -mavx512vl -mavx512dq \
    -O0 -DVMOV=2 -DONELINE -o avx512_scatter.out \
    avx_test.c -lm 

echo "Running avx512_gather/scatter singleline for flush"
taskset -c $CPU_CORE ./avx512_scatter.out > data/gather_scatter_singleline_time.csv

python3 run_dudect.py -f $output_file 


output_file=data/gather_scatter_multiline_time.csv
csv_file=${output_file}"_dudect_results.csv"
[ -e $output_file ] && mv $output_file $output_file.bk
[ -e $csv_file ] && mv $csv_file $csv_file.bk

gcc -mavx2 -mavx512f -mavx512bw -mavx512vl -mavx512dq \
    -O0 -DVMOV=2 -o avx512_scatter.out \
    avx_test.c -lm 

echo "Running avx512_gather/scatter multiline for flush"
taskset -c $CPU_CORE ./avx512_scatter.out > data/gather_scatter_multiline_time.csv

python3 run_dudect.py -f $output_file 


# ===================
# T-test for AVX512 gather/scatter operations with varying indices
# ===================

output_file=data/gather_scatter_index_time.csv
csv_file=${output_file}"_dudect_results.csv"
[ -e $output_file ] && mv $output_file $output_file.bk
[ -e $csv_file ] && mv $csv_file $csv_file.bk

gcc -mavx2 -mavx512f -mavx512bw -mavx512vl -mavx512dq \
    -O0 -o avx512_index_scatter.out \
    avx_index_test.c -lm 

echo "Running avx512_gather/scatter index multiline for flush"
taskset -c $CPU_CORE ./avx512_index_scatter.out > $output_file

python3 run_dudect.py -f $output_file


# ===================
# T-test for AVX512 packed load/store operations in word level dependency
# ===================

output_file="data/packed_load_store_false_dependency.csv"
csv_file=${output_file}"_dudect_results.csv"
[ -e $output_file ] && mv $output_file $output_file.bk
[ -e $csv_file ] && mv $csv_file $csv_file.bk

gcc -mavx2 -mavx512f -mavx512bw -mavx512vl -mavx512dq \
    -O0 -o test-false-mask.out \
    test-false-mask.c -lm -lpthread

echo "Running Avx512 load for false dependency"
taskset -c $CPU_CORE ./test-false-mask.out >> $output_file

gcc -DWRITE \
    -mavx2 -mavx512f -mavx512bw -mavx512vl -mavx512dq \
    -O0 -o test-false-mask.out \
    test-false-mask.c -lm -lpthread

echo "Running avx512 store for false dependency" 
taskset -c $CPU_CORE ./test-false-mask.out >> $output_file

python3 run_dudect.py -f $output_file


# ===================
# T-test for AVX512 gather/scatter operations in word level dependency
# ===================

output_file="data/gather_scatter_false_dependency.csv"
csv_file=${output_file}"_dudect_results.csv"
[ -e $output_file ] && mv $output_file $output_file.bk
[ -e $csv_file ] && mv $csv_file $csv_file.bk

gcc -mavx2 -mavx512f -mavx512bw -mavx512vl -mavx512dq \
    -O0 -g -o avx_index_test_false_depen.out \
    avx_index_test_false_depen.c -lm -lpthread

echo "Running avx512_gather/scatter index for false dependency"
taskset -c $CPU_CORE ./avx_index_test_false_depen.out > $output_file

python3 run_dudect.py -f $output_file

# ===================
# Generate figures 
# ===================

python3 dudect_pic.py
python3 dudect_pic_index.py
