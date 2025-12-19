#!/bin/bash

cd ../..
. ./setup.sh
cd - > /dev/null

set -e
set -x

if [ -z "$SKIP" ]; then

DIR=`pwd`
cd ../../passes
rm -f ./dfl/dfl.so ./dfl/dfl.o 
make clean install || exit 1
cd ../lib
rm -f ./dfl/dfl.o
# generate all the libs we need to use the different versions
ARCH='westmere' make clean install || exit 2
cp ../bin/cfl.bcc ../bin/cfl.noavx.bcc
cp ../bin/dfl.bcc ../bin/dfl.noavx.bcc
ARCH='skylake-avx512' make clean install || exit 2
cp ../bin/cfl.bcc ../bin/cfl.avx512.bcc
cp ../bin/dfl.bcc ../bin/dfl.avx512.bcc
# ARCH='native' make clean install || exit 2
cd $DIR

fi

OPT=${OPT:-3}

runonsimple()
{
	file1="src/"$1".c"
	cc="clang"
	cflags=""
	ldflags=""
	base="src/"$1
	name=${file1%.*}
	output=$name".taint"
	exe1=$name".out"
	origexe1=$name".orig"
	noavxexe1=$name".noavx"
	avx512exe1=$name".avx512"
	stat=$base".txt"
	bc1=$name".linked.bc"
	out1=$name".final.bc"

	rm -f $exe1 $bc1 $out1

	# compile orig and dft versions
	dft_start=$(date +%s%3N)
	OPT=$OPT ./scripts/build_dft.sh $file1 $cc "$cflags" "$ldflags"
	dft_end=$(date +%s%3N)

	# original version
	cp $bc1 $origexe1.bc
	mv $origexe1.out $origexe1-bk.out
	llvm-dis $origexe1.bc -o $origexe1.ll

	OPT=$OPT ./scripts/targeted_cfl_dump_loops.sh $file1 $cc

	# no AVX version for gem5
	OPT=$OPT DFL_AVX=" " NOAVX="-mno-avx -mno-sse -mno-avx2" AVX_VER=".noavx" ./scripts/targeted_cfl.sh $file1 $cc
	mv $out1 $noavxexe1.bc
	# mv $exe1 $noavxexe1.out
	llvm-dis $noavxexe1.bc -o $noavxexe1.ll

	# AVX512 version for our server
	lin_start=$(date +%s%3N)
	OPT=$OPT DFL_AVX="-dfl-avx512=1" NOAVX="-march=skylake-avx512" AVX_VER=".avx512" ./scripts/targeted_cfl.sh $file1 $cc
	lin_end=$(date +%s%3N)
	mv $out1 $avx512exe1.bc
	# mv $exe1 $avx512exe1.out
	llvm-dis $avx512exe1.bc -o $avx512exe1.ll
}

c_benchmarks="AES ARC4 Blowfish CAST DES DES3 XOR"

# if [[ $(< /proc/sys/kernel/perf_event_paranoid ) != "-1" ]]; then
# 	echo "DISABLING perf_event_paranoid"
# 	echo '-1' | tee /proc/sys/kernel/perf_event_paranoid 
# fi

# if [[ $(< /proc/sys/kernel/randomize_va_space ) != "0" ]]; then
# 	echo "DISABLING ASLR to check linearized accesses"
# 	echo 0 | tee /proc/sys/kernel/randomize_va_space
# fi

runonsimple $1
