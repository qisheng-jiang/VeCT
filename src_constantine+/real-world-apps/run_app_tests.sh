#!/bin/bash

set -e
set -x

ROOT=/app/src_constantine+

cd $ROOT/real-world-apps/binsec 
bash run_test.sh

cd $ROOT/real-world-apps/issta2018-benchmarks-wu
bash run_test.sh

cd $ROOT/real-world-apps/pycrypto
bash run_test.sh 

cd $ROOT/real-world-apps/
python3 stats.py
