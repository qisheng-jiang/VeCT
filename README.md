# VeCT: Secure and Efficient Constant-Time Code Rewriting with Vector Extensions

This repository contains the source code and experimental artifacts for **VeCT**, a tool designed to automatically rewrite code to be constant-time by leveraging modern CPU vector extensions like AVX-512. VeCT aims to provide strong security guarantees against timing-based side-channel attacks while maintaining high performance.


## Citation

```bibtex
@inproceedings{sec2026vect,
  title={{VeCT}: Secure and Efficient Constant-Time Code Rewriting with Vector Extensions},
  author={Qisheng Jiang and Danfeng Zhang},
  booktitle={Proceedings of the 35th USENIX Security Symposium (USENIX Security 26)},
  year={2026},
  publisher={USENIX Association}
}
```


## Directory Structure

The repository is organized as follows:

```
.
├── src/
│   ├── microbenchmark/
│   │   └── # Code and scripts for microbenchmark evaluations
│   ├── real-world-apps/
│   │   └── # Code and scripts for real-world application tests
│   └── # Core source code for the VeCT tool
└── t-test/
    └── # Scripts and data for the t-test analysis of AVX-512 memory access
```

  - **`./src/`**: This directory contains the primary source code for the VeCT tool and its evaluation benchmarks.

      - **`./src/microbenchmark/`**: Contains all code, scripts, and data related to the microbenchmark experiments used to evaluate the fine-grained performance of VeCT's transformations.
      - **`./src/real-world-apps/`**: Contains the versions of real-world applications (e.g., cryptographic libraries, data processing utilities) that were tested and evaluated with VeCT.

  - **`./t-test/`**: This directory contains the necessary scripts, measurement data, and analysis code to reproduce our statistical side-channel assessment (t-test) concerning the constant-time guarantees for AVX-512 memory access patterns.

## Getting Started

> [!NOTE]
> Please refer to [Evaluation.md](./Evaluation.md) for **Artifact Evaluation**. 


### Installation & Building

1.  **Clone the repository:**

    ```bash
    git clone https://github.com/qisheng-jiang/VeCT
    cd VeCT
    ```

2.  **Setup environment:**
    
    ```bash 
    docker build -t image-vect .
    docker run --rm -it -v "$(pwd)":/app image-vect /bin/bash
    cd src
    ./install.sh
    . ./setup.sh
    ./llvm_compile_dfsan_cpp.sh
    apt update -y
    apt upgrade -y 
    apt install -y llvm-13 clang-13 
    ```

3.  **Build VeCT:**
    ```bash 
    cd ./src/passes && make install
    cd ./src/lib && make install 
    ```

## How to Run VeCT

### Running the Constant-time Guarantees Assessment (t-test)

To reproduce the t-test results for AVX-512 memory access:

```bash
cd ./t-test
bash t-test.sh
```

The results will be generated in the [`./t-test/data/`](./t-test/data/) subdirectory. 

### Running the Benchmarks

You can evaluate VeCT's performance and security on both microbenchmarks and real-world applications.

1.  **Microbenchmarks:**

    ```bash
    cd ./src/microbenchmark
    # Overview and Impact of Access Count
    bash run_microbenchmarks.sh
    # Security Validation
    bash run_validation.sh 
    ```

The results will be generated in the following subdirectories: 
- [`./src/microbenchmark/vector-perf-output`](./src/microbenchmark/vector-perf-output)
- [`./src/microbenchmark/vector-t-test-output`](./src/microbenchmark/vector-t-test-output) 
- [`./src/microbenchmark/vector-t-test-false-depen-output`](./src/microbenchmark/vector-t-test-false-depen-output)


2.  **Real-World Applications:**

    ```bash
    cd ./src/real-world-apps
    bash run_app_tests.sh
    ```

The results will be generated in the following subdirectories: 
- [`./src/real-world-apps/binsec/output`](./src/real-world-apps/binsec/output) 
- [`./src/real-world-apps/issta2018-benchmarks-wu/output`](./src/real-world-apps/issta2018-benchmarks-wu/output) 
- [`./src/real-world-apps/pycrypto/output`](./src/real-world-apps/pycrypto/output) 


## How to Run Experiments 

See [Evaluation.md](./Evaluation.md) for more details. 



## Acknowledgments

We are grateful to the authors of the [Constantine](https://github.com/pietroborrello/constantine) framework for their open-source contribution.
