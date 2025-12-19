#!/bin/bash
# set -e
# set -x
# set -u

file="$1"
cc="clang"
PROJECT_NAME=${file%.*}
LIBNAME="binsec"
LIBBC=${LIBBC:-""}

OPT=${OPT:-2}

# we take optimizations from O1, but we exclude -loop-unroll, -jump-threading, -pgo-memop-opt and -loop-unswitch as they usually create more complex CFGs
SAFE_OPTS='-tti -tbaa -scoped-noalias -assumption-cache-tracker -targetlibinfo -verify -ee-instrument -simplifycfg -domtree -sroa -early-cse -lower-expect -tti -tbaa -scoped-noalias -assumption-cache-tracker -profile-summary-info -forceattrs -inferattrs -ipsccp -called-value-propagation -attributor -globalopt -domtree -mem2reg -deadargelim -domtree -basicaa -aa -loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -simplifycfg -basiccg -globals-aa -prune-eh -always-inline -functionattrs -domtree -sroa -basicaa -aa -memoryssa -early-cse-memssa -speculative-execution -basicaa -aa -lazy-value-info -correlated-propagation -simplifycfg -domtree -basicaa -aa -loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -libcalls-shrinkwrap -loops -branch-prob -block-freq -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -basicaa -aa -loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -tailcallelim -simplifycfg -reassociate -domtree -loops -loop-simplify -lcssa-verification -lcssa -basicaa -aa -scalar-evolution -loop-rotate -licm -simplifycfg -domtree -basicaa -aa -loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -loop-simplify -lcssa-verification -lcssa -scalar-evolution -indvars -loop-idiom -loop-deletion -phi-values -memdep -memcpyopt -sccp -demanded-bits -bdce -basicaa -aa -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -lazy-value-info -correlated-propagation -basicaa -aa -phi-values -memdep -dse -loops -loop-simplify -lcssa-verification -lcssa -basicaa -aa -scalar-evolution -licm -postdomtree -adce -simplifycfg -domtree -basicaa -aa -loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -barrier -basiccg -rpo-functionattrs -globalopt -globaldce -basiccg -globals-aa -float2int -domtree -loops -loop-simplify -lcssa-verification -lcssa -basicaa -aa -scalar-evolution -loop-rotate -loop-accesses -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -loop-distribute -branch-prob -block-freq -scalar-evolution -basicaa -aa -loop-accesses -demanded-bits -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -loop-simplify -scalar-evolution -aa -loop-accesses -lazy-branch-prob -lazy-block-freq -loop-load-elim -basicaa -aa -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -simplifycfg -domtree -basicaa -aa -loops -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -loop-simplify -lcssa-verification -lcssa -scalar-evolution -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instcombine -loop-simplify -lcssa-verification -lcssa -scalar-evolution -licm -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -transform-warning -alignment-from-assumptions -strip-dead-prototypes -domtree -loops -branch-prob -block-freq -loop-simplify -lcssa-verification -lcssa -basicaa -aa -scalar-evolution -block-freq -lazy-branch-prob -lazy-block-freq -opt-remark-emitter -instsimplify -div-rem-pairs -simplifycfg -targetlibinfo -domtree -loops -branch-prob -block-freq'

if [ $OPT -eq 0 ]; then
CFLAGS="-g -Og -mllvm -x86-cmov-converter=0 "
OFLAGS="-O0"
LDFLAGS="-Og"
else
CFLAGS="-O$OPT -fno-unroll-loops -mllvm -x86-cmov-converter=0 -g"
OFLAGS="-O$OPT --disable-loop-unrolling"
LDFLAGS="-O$OPT "
fi

# DFSAN_ABILIST=`$cc -fsanitize=dataflow -c $file -### 2>&1 | grep sanitize | sed "s/.*-fsanitize-blacklist=\([^\"]*\).*/\1/g"`

# cat $DFSAN_ABILIST ../../lib/dft/dfsan.abilist > dfsan_abilist.txt

clang -O1 -g -fno-delete-null-pointer-checks -fno-unroll-loops -flto -c -I./inc -o $PROJECT_NAME.base.bc $file -fno-exceptions
llvm-link -o $PROJECT_NAME.linked.bc $PROJECT_NAME.base.bc $LIBBC
opt -internalize -internalize-public-api-list=main -globaldce -o $PROJECT_NAME.linked_int.bc $PROJECT_NAME.linked.bc
$cc  $CFLAGS -fPIC -DPIC -o $PROJECT_NAME.orig.out -ldl -lm -pthread $PROJECT_NAME.linked_int.bc
