import math
import pandas as pd
import itertools
import argparse

class TTestCtx:
    def __init__(self):
        self.mean = [0.0, 0.0]
        self.m2 = [0.0, 0.0]
        self.n = [0.0, 0.0]

    def push(self, x, clazz):
        assert clazz == 0 or clazz == 1
        self.n[clazz] += 1
        delta = x - self.mean[clazz]
        self.mean[clazz] += delta / self.n[clazz]
        self.m2[clazz] += delta * (x - self.mean[clazz])

    def compute(self):
        var = [0.0, 0.0]
        if self.n[0] < 2 or self.n[1] < 2:
            return 0.0  # Not enough data
        var[0] = self.m2[0] / (self.n[0] - 1)
        var[1] = self.m2[1] / (self.n[1] - 1)
        num = self.mean[0] - self.mean[1]
        den = math.sqrt(var[0] / self.n[0] + var[1] / self.n[1])
        if den == 0:
            return 0.0
        return num / den

def report(t_val, n0, n1):
    t_threshold_bananas = 500
    t_threshold_moderate = 10
    num_measurements = n0 + n1
    max_tau = abs(t_val) / math.sqrt(num_measurements) if num_measurements > 0 else 0
    print(f"meas: {num_measurements/1e6:.2f} M, ", end="")
    if num_measurements < 10000:
        print(f"not enough measurements ({10000-num_measurements:.0f} still to go).")
        return (t_val, -1)
    print(f"max t: {t_val:+7.2f}, max tau: {max_tau:.2e}, (5/tau)^2: {(5*5)/(max_tau*max_tau) if max_tau != 0 else float('inf'):.2e}.", end="")
    if abs(t_val) > t_threshold_bananas:
        print(" Definitely not constant time.")
        return (t_val, 2)
    elif abs(t_val) > t_threshold_moderate:
        print(" Probably not constant time.")
        return (t_val, 1)
    else:
        print(" For the moment, maybe constant time.")
        return (t_val, 0)

def dudect_main_py(group0, group1):
    """
    group0, group1: two equal-length lists or arrays of timing data
    """
    assert len(group0) == len(group1), "Both groups must have the same length."
    n = len(group0)
    ttest = TTestCtx()
    # Interleave the data as in dudect, but you can also just push all group0 as class 0, all group1 as class 1
    for x in group0:
        ttest.push(x, 0)
    for x in group1:
        ttest.push(x, 1)
    t_val = ttest.compute()
    return report(t_val, ttest.n[0], ttest.n[1])

def run_dudect(input_file):
    skip_nums = 0
    if 'index' in input_file:
        skip_nums = 11
    # read CSV
    max_columns = 3+10000+100000 
    df = pd.read_csv(input_file, header=None, 
                     usecols=range(max_columns),
                     skiprows=skip_nums)
    grouped = df.groupby([0, 2])
    print(len(grouped))

    results = []

    for (group_key_1, group_key_2), group_df in grouped:

        for i, j in itertools.combinations(group_df.index, 2):
            mask_1 = group_df.loc[i][1]
            mask_2 = group_df.loc[j][1]

            row1 = group_df.loc[i].tolist()[3+10000:]
            row2 = group_df.loc[j].tolist()[3+10000:]
            print(f"Processing groups: {group_key_1}, {group_key_2} with masks: {mask_1}, {mask_2}")
            # Call the dudect main function
            ret = dudect_main_py(row1, row2)
            
            results.append({
                "group": (group_key_1, group_key_2),
                "mask 1": mask_1,
                "mask 2": mask_2,
                "t_value": ret[0],
                "difference_detected": ret[1]})

    results_df = pd.DataFrame(results)
    results_df.to_csv(input_file + "_dudect_results.csv", index=False)
    return results

def run_dudect_dependency(input_file):
    # read CSV
    max_columns = 2+10000+100000 
    df = pd.read_csv(input_file, header=None, usecols=range(max_columns))
    grouped = df.groupby(0)
    print(len(grouped))

    results = []

    for group_key_1, group_df in grouped:

        for i, j in itertools.combinations(group_df.index, 2):
            mask_1 = group_df.loc[i][1]
            mask_2 = group_df.loc[j][1]

            row1 = group_df.loc[i].tolist()[2+10000:]
            row2 = group_df.loc[j].tolist()[2+10000:]
            print(f"Processing groups: {group_key_1} with masks: {mask_1}, {mask_2}")
            # Call the dudect main function
            ret = dudect_main_py(row1, row2)
                
            results.append({
                "group": group_key_1,
                "mask 1": mask_1,
                "mask 2": mask_2,
                "t_value": ret[0],
                "difference_detected": ret[1]})

    results_df = pd.DataFrame(results)
    results_df.to_csv(input_file + "_dudect_results.csv", index=False)
    return results

def main():
    parser = argparse.ArgumentParser(description="Dudect Python Version")
    parser.add_argument("-f", "--file", required=True, help="Path to the CSV input file")
    args = parser.parse_args()

    if "false_dependency" in args.file:
        run_dudect_dependency(args.file)
    else:
        run_dudect(args.file)


if __name__ == "__main__":
    main()
