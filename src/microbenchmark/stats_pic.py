import re
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import os

# ================= CONFIGURATION =================
# Define your file labels and paths here.
# Format: 'Label Name': 'File Path'
# NOTE: One label MUST contain the word "insecure" to serve as the baseline.
file_mapping = {
    'insecure': '/app/src_constantine+/microbenchmark/vector-perf-output/insecure-64.res',
    'constantine-64': '/app/src_constantine+/microbenchmark/vector-perf-output/constantine-64.res',
    'constantine-4': '/app/src_constantine+/microbenchmark/vector-perf-output/constantine-4.res',
    'single-64': '/app/src/microbenchmark/vector-perf-output/false-64.res',
    'single-4': '/app/src/microbenchmark/vector-perf-output/false-4.res',
    'vector-64': '/app/src/microbenchmark/vector-perf-output/true-64.res',
    'vector-4': '/app/src/microbenchmark/vector-perf-output/true-4.res',
}

# Directory to save the output images
output_dir = 'vector-perf-output'
# ===============================================

def parse_file(filepath, label):
    """Parses a single file to extract configuration and Median values."""
    data = []
    
    # Regex to match the header line
    # Example: == Running test with array_size=10 and update_size=6 for uint32_t with is_load=0 ==
    header_pattern = re.compile(r"== Running test with .*?array_size=(\d+) and update_size=(\d+) for (uint\d+_t) with is_load=(\d+) ==")
    
    current_params = {}
    
    try:
        with open(filepath, 'r') as f:
            lines = f.readlines()
            
        for line in lines:
            line = line.strip()
            
            # Check for header line
            header_match = header_pattern.search(line)
            if header_match:
                current_params = {
                    'Label': label,
                    'Array_Size': int(header_match.group(1)),
                    'Update_Size': int(header_match.group(2)),
                    'Data_Type': header_match.group(3),
                    'Is_Load': int(header_match.group(4))
                }
            
            # Check for Median line
            if line.startswith('Median:') and current_params:
                # Extract the numeric value
                median_value = float(line.split(':')[1].strip())
                entry = current_params.copy()
                entry['Median'] = median_value
                data.append(entry)
                # Clear params to prevent duplicate reading for the same header
                current_params = {}
                
    except FileNotFoundError:
        print(f"Error: File {filepath} not found. Please check the path.")
        return []
        
    return data

def main():
    # 1. Parse all files
    all_data = []
    print("Starting file parsing...")
    for label, filepath in file_mapping.items():
        print(f"Processing: {label} -> {filepath}")
        file_data = parse_file(filepath, label)
        all_data.extend(file_data)
    
    if not all_data:
        print("No data found. Please check your file content format.")
        return

    df = pd.DataFrame(all_data)
    
    # 2. Calculate Overhead
    # Identify the baseline label (must contain 'insecure')
    baseline_label = next((k for k in file_mapping.keys() if 'insecure' in k.lower()), None)
    if not baseline_label:
        print("Error: Could not find a label containing 'insecure' in file_mapping. Cannot calculate overhead.")
        return

    print(f"Baseline detected: {baseline_label}")
    
    # Separate baseline data from others
    df_base = df[df['Label'] == baseline_label].copy()
    df_others = df[df['Label'] != baseline_label].copy()
    df_others = df_others.sort_values('Label', ascending=False)
    
    # Rename Median column for merging
    df_base = df_base[['Array_Size', 'Update_Size', 'Data_Type', 'Is_Load', 'Median']]
    df_base = df_base.rename(columns={'Median': 'Baseline_Median'})
    
    # Merge data to align rows by parameters
    merged_df = pd.merge(df_others, df_base, 
                         on=['Array_Size', 'Update_Size', 'Data_Type', 'Is_Load'], 
                         how='left')
    
    # Calculate Overhead Formula: (Value - Baseline) / Baseline
    merged_df['Overhead'] = merged_df['Median'] / merged_df['Baseline_Median']
    
    # Create output directory
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # 3. Plotting Logic
    # Group by Data_Type (uint32/64) and Is_Load (0/1)
    
    data_types = merged_df['Data_Type'].unique()
    is_loads = merged_df['Is_Load'].unique()
    
    # Set plot style
    sns.set_theme(style="whitegrid")
    
    plot_count = 0
    
    for stride in ['-64', '-4']:
        for dtype in data_types:
            for load_flag in is_loads:
                # Filter subset for current configuration
                subset = merged_df[(merged_df['Data_Type'] == dtype) & (merged_df['Is_Load'] == load_flag) & (merged_df['Label'].str.endswith(stride))]
                
                if subset.empty:
                    continue
                    
                # --- Scenario 1: Fixed Update Size = 6, Vary Array Size ---
                scenario1_data = subset[subset['Update_Size'] == 6].sort_values('Array_Size')
                
                if not scenario1_data.empty:
                    plt.figure(figsize=(10, 6))
                    sns.barplot(data=scenario1_data, x='Array_Size', y='Overhead', hue='Label')
                    
                    plt.title(f"Overhead vs Array Size\n(Update_Size=6, {dtype}, {'load' if load_flag else 'store'})")
                    plt.ylabel('Overhead (Normalized, Log Scale)')
                    plt.yscale('log')
                    plt.xlabel('Array Size')
                    plt.legend(title='Variant')
                    
                    # Save plot
                    fname = f"{dtype}_{'load' if load_flag else 'store'}_{stride}_vary_array_size.png"
                    plt.savefig(os.path.join(output_dir, fname))
                    plt.close()
                    plot_count += 1
                    print(f"Saved plot: {fname}")

                # --- Scenario 2: Fixed Array Size = 1000, Vary Update Size ---
                scenario2_data = subset[subset['Array_Size'] == 1000].copy()
                
                # Filter Update Size based on integer type
                if 'uint32' in dtype:
                    # Range 2 to 15
                    scenario2_data = scenario2_data[(scenario2_data['Update_Size'] >= 2) & (scenario2_data['Update_Size'] <= 15)]
                elif 'uint64' in dtype:
                    # Range 2 to 7
                    scenario2_data = scenario2_data[(scenario2_data['Update_Size'] >= 2) & (scenario2_data['Update_Size'] <= 7)]
                
                scenario2_data = scenario2_data.sort_values('Update_Size')

                if not scenario2_data.empty:
                    plt.figure(figsize=(10, 6))
                    sns.barplot(data=scenario2_data, x='Update_Size', y='Overhead', hue='Label')
                    
                    plt.title(f"Overhead vs Update Size\n(Array_Size=1000, {dtype}, {'load' if load_flag else 'store'})")
                    plt.ylabel('Overhead (Normalized)')
                    plt.xlabel('Update Size')
                    plt.legend(title='Variant')
                    
                    # Save plot
                    fname = f"{dtype}_{'load' if load_flag else 'store'}_{stride}_vary_update_size.png"
                    plt.savefig(os.path.join(output_dir, fname))
                    plt.close()
                    plot_count += 1
                    print(f"Saved plot: {fname}")

    print(f"\nDone! Generated {plot_count} plots in the '{output_dir}' directory.")

# ===============================================
# Dummy File Generator (For testing purposes only)
# Comment this function out when using real data.
# ===============================================
def create_dummy_files():
    import random
    types = ['uint32_t', 'uint64_t']
    loads = [0, 1]
    
    # Define Configurations
    configs = []
    # Scene 1 configs (Vary Array Size)
    for sz in [10, 100, 1000, 5000, 10000]:
        configs.append((sz, 6))
    # Scene 2 configs (Vary Update Size)
    for us in range(2, 16):
        configs.append((1000, us))
        
    for label, fname in file_mapping.items():
        with open(fname, 'w') as f:
            for t in types:
                for l in loads:
                    for arr_sz, upd_sz in configs:
                        # Generate base values
                        base = 10000 + arr_sz * 0.1
                        if label == 'insecure':
                            val = base
                        else:
                            # Add random overhead
                            overhead = random.uniform(0.1, 0.5)
                            val = base * (1 + overhead)
                            
                        f.write(f"== Running test with array_size={arr_sz} and update_size={upd_sz} for {t} with is_load={l} ==\n")
                        f.write(f"Count: 10\n")
                        f.write(f"Average: {val}\n")
                        f.write(f"Median:  {val}\n\n")

if __name__ == "__main__":
    # UNCOMMENT the line below to generate test files if you don't have real files yet
    # create_dummy_files() 
    
    main()

