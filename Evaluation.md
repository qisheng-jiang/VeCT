# Artifact Evaluation 


## Hardware dependencies

Intel CPU supporting AVX-512 

**How to check** 
```bash 
lscpu | grep avx512
```

The output should look like the following:
```bash 
> Flags: ... avx512* ...
```


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

- **Overview and Impact of Access Count** 

```bash
# run VeCT (Vector and Single)
cd /app/src/microbenchmark
bash run_microbenchmarks.sh
# run baselines (Constantine+ and Original insecure code)
cd /app/src_constantine+/microbenchmark
bash run_microbenchmarks.sh
# generate results 
cd /app/src/microbenchmark
python3 stats_pic.py 
```

[`./src/microbenchmark/results.md`](./src/microbenchmark/results.md) shows the reproduced results for Figures 15-16 in the paper. 

- **Security Validation** 

```bash
cd /app/src/microbenchmark
bash run_validation.sh 
```

[`./src/microbenchmark/validation.md`](./src/microbenchmark/validation.md) shows the reproduced results for security validation in the paper. 

The output should look like the following for each setup:

```bash
> Processing groups: [xxx] with masks: index 0, index 1
> meas: [0.00] M, max t:   [+0.30], max tau: [6.71e-02], (5/tau)^2: [5.56e+03]. For the moment, maybe constant time.
```

## Real-World Applications (Section 6.2)

```bash 
# run VeCT (Vector and Single)
cd /app/src/real-world-apps
bash run_app_tests.sh
# run baselines (Constantine+ and Original insecure code)
cd /app/src_constantine+/real-world-apps
bash run_app_tests.sh
# generate results 
cd /app/src/real-world-apps
python3 stats_table.py 
```

[`./src/real-world-apps/results.md`](./src/real-world-apps/results.md) shows the reproduced results for Table 1 in the paper. 

