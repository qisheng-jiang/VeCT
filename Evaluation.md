# Artifact Evaluation 


## Hardware dependencies

Intel CPU supporting AVX-512 


## Software dependencies

We recommande using Docker for setting up environment smoothly. 

> Please refer to [Docker](https://docs.docker.com/get-started/) to see more details about how to install Docker. 


## Setup 

```bash 
docker build -t image-vect .
docker run --rm -it -v "$(pwd)":/app image-vect /bin/bash
# execute the following in the docker 
cd /app/src
./install.sh
. ./setup.sh
./llvm_compile_dfsan_cpp.sh
apt update -y
apt upgrade -y 
apt install -y llvm-13 clang-13 
```

## Build VeCT

```bash
cd /app/src/passes && make install -j10
cd /app/src/lib && make install -j10
```

### Build Baseline (Constantine+)

```bash 
cd /app/src_constantine+
. ./setup.sh
cd /app/src_constantine+/passes && make install -j10
cd /app/src_constantine+/lib && make install -j10
```

## T-test (Section 4)

```bash
cd /app/t-test
bash t-test.sh
```

[`./t-test/results.md`](./t-test/results.md) shows the reproduced results for Figures 2-5 in the paper. 

## Microbenchmarks (Section 6.1)

### Overview and Impact of Access Count 

```bash
# run VeCT (Vector and Single)
cd /app/src/microbenchmark
bash run_microbenchmarks.sh
# run baselines (Constantine+ and Origianl insecure code)
cd /app/src_constantine+/microbenchmark
bash run_microbenchmarks.sh
# generate results 
cd /app/src/microbenchmark
python3 stats_pic.py 
```

[`./src/microbenchmark/results.md`](./src/microbenchmark/results.md) shows the reproduced results for Figures 15-16 in the paper. 

### Security Validation 




