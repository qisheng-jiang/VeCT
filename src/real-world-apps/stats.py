import os
import re
import statistics
import pandas as pd

def parse_file(file_path):
    with open(file_path, 'r') as f:
        content = f.read()

    benchmarks = {}
    current_stride = None
    current_benchmark = None
    current_mode = None

    for line in content.strip().splitlines():
        line = line.strip()

        # Detect benchmark and stride size
        if line.startswith("== Run"):
            match = re.search(r"Run ([^ ]+) with stride size (\d+)", line)
            if match:
                current_benchmark = match.group(1)
                current_stride = int(match.group(2))
                benchmarks.setdefault(current_benchmark, {}).setdefault(current_stride, {})
        
        # Detect mode (noavx, avx512, vector)
        elif line.startswith("run_noperf:"):
            match = re.search(r"\.([^.]+)\.out", line)
            if match:
                current_mode = match.group(1)

        # Parse elapsed times
        elif "Elapsed time" in line:
            match = re.search(r"Elapsed time .*: (\d+)", line)
            if match and current_benchmark and current_mode and current_stride is not None:
                val = int(match.group(1))
                benchmarks[current_benchmark][current_stride].setdefault(current_mode, []).append(val)

    return benchmarks

def merge_results(all_results):
    columns = ['Benchmark']
    modes = ['vector', 'avx512', 'noavx']
    for stat in ['avg_64', 'avg_4', 'med_64', 'med_4']:
        for mode in modes:    
            columns.append(f"{mode}_{stat}")

    rows = []

    for benchmark, stride_data in all_results.items():
        row = [benchmark]
        for stat_type in ['avg', 'med']:
            for stride in [64, 4]:
                for mode in modes:
                    values = stride_data.get(stride, {}).get(mode, [])
                    if values:
                        value = statistics.mean(values) if stat_type == 'avg' else statistics.median(values)
                        value = value / 10000
                    else:
                        value = ''
                    row.append(value)
        rows.append(row)

    return pd.DataFrame(rows, columns=columns)

def main(inputs, output_csv):
    combined_results = {}

    for filepath in inputs:
        parsed = parse_file(filepath)
        # Merge into combined_results
        combined_results.update(parsed)
        # for bench, strides in parsed.items():
        #     for stride, modes in strides.items():
        #         for mode, values in modes.items():
        #             combined_results.setdefault(bench, {}).setdefault(stride, {}).setdefault(mode, []).extend(values)

    df = merge_results(combined_results)
    df.to_csv(output_csv, index=False)
    print(f"Saved summary CSV to: {output_csv}")

if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    file_version = '8-5'
    file_dirs = ['binsec', 'issta2018-benchmarks-wu', 'pycrypto']
    input_files = [os.path.join(script_dir, d, 'output', f'run_test-{file_version}.log') for d in file_dirs]
    output_csv = os.path.join(script_dir, f'stats_summary_{file_version}.csv')
    main(input_files, output_csv)

