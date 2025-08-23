#!/bin/bash

cd binsec 
bash run_test.sh

cd ../
cd issta2018-benchmarks-wu
bash run_test.sh

cd ../
cd pycrypto
bash run_test.sh 

cd ../
python3 stats.py
