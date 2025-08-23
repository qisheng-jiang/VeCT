# VeCT: Secure and Efficient Constant-Time Code Rewriting with Vector Extensions

This repository contains the source code and experimental artifacts for **VeCT**, a tool designed to automatically rewrite code to be constant-time by leveraging modern CPU vector extensions like AVX-512. VeCT aims to provide strong security guarantees against timing-based side-channel attacks while maintaining high performance.

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

  - **`./t-test/`**: This directory contains the necessary scripts, measurement data, and analysis code to reproduce our statistical leakage assessment (t-test) concerning the constant-time guarantees for AVX-512 memory access patterns.

## Getting Started

### Installation & Building

1.  **Clone the repository:**

    ```bash
    git clone VeCT
    cd VeCT
    ```

2.  **Build the VeCT tool and benchmarks:**
    
    Refer to [README](./OLD_README.md). 

## How to Run Experiments

### Running the Leakage Assessment (t-test)

To reproduce the t-test results for AVX-512 memory access:

```bash
cd t-test
bash t-test.sh
```

The results will be generated in the `data/` subdirectory.

### Running the Benchmarks

You can evaluate VeCT's performance and security on both microbenchmarks and real-world applications.

1.  **Microbenchmarks:**

    ```bash
    cd src/microbenchmark
    ./run_microbenchmarks.sh
    ```

2.  **Real-World Applications:**

    ```bash
    cd src/real-world-apps
    ./run_app_tests.sh
    ```

