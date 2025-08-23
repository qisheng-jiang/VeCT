from statistics import mean, median
from collections import defaultdict
import sys

log_file = "vector-perf-single/1.log"

if len(sys.argv) > 1:
    log_file = sys.argv[1]

data = defaultdict(list)
current_key = None

with open(log_file, "r") as f:
    for line in f:
        line = line.strip()
        if line.startswith("Running test with"):
            current_key = line 
        elif "Elapsed time" in line:
            match = line.split(": ")[-1]
            data[current_key].append(int(match))

for key, values in data.items():
    print(f"== {key} ==")
    print(f"Count: {len(values)}")
    print(f"Average: {mean(values):.2f}")
    print(f"Median:  {median(values)}")
    print()
