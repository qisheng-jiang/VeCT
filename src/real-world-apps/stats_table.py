import pandas as pd
import numpy as np
import os

def manual_markdown_table(df):
    lines = []
    columns = df.columns
    
    header = "| " + " | ".join(columns) + " |"
    lines.append(header)

    separator = "| " + " | ".join(["---"] * len(columns)) + " |"
    lines.append(separator)
    
    for _, row in df.iterrows():
        row_str = "| " + " | ".join(map(str, row.values)) + " |"
        lines.append(row_str)
        
    return "\n".join(lines)

def process_benchmark_data(file1_path, file2_path, output_md_path):
    df1 = pd.read_csv(file1_path)
    df2 = pd.read_csv(file2_path)

    df1_clean = df1.rename(columns={
        'vector_med_64': 'val_vector_64', 'vector_med_4': 'val_vector_4',
        'avx512_med_64': 'val_single_64', 'avx512_med_4': 'val_single_4'
    })[['Benchmark', 'val_vector_64', 'val_vector_4', 'val_single_64', 'val_single_4']]

    df2_clean = df2.rename(columns={
        'avx512_med_64': 'val_const_64',  'avx512_med_4': 'val_const_4',
        'orig-bk_med_64': 'val_base_64',  'orig-bk_med_4': 'val_base_4'
    })[['Benchmark', 'val_const_64', 'val_const_4', 'val_base_64', 'val_base_4']]

    merged_df = pd.merge(df1_clean, df2_clean, on='Benchmark')

    rows = []
    for size in ['4', '64']:
        base_col, const_col = f'val_base_{size}', f'val_const_{size}'
        vec_col, sing_col   = f'val_vector_{size}', f'val_single_{size}'

        for _, row in merged_df.iterrows():
            oh_const = row[const_col] / row[base_col]
            oh_vector = row[vec_col] / row[base_col]
            oh_single = row[sing_col] / row[base_col]
            
            rows.append({
                'Benchmark': row['Benchmark'],
                'Size': int(size),
                'Overhead Constantine': oh_const,
                'Overhead Vector': oh_vector,
                'Overhead Single': oh_single
            })

    result_df = pd.DataFrame(rows)

    geomean_rows = []

    for size, group in result_df.groupby('Size'):
        geo_const = np.exp(np.mean(np.log(group['Overhead Constantine'])))
        geo_vector = np.exp(np.mean(np.log(group['Overhead Vector'])))
        geo_single = np.exp(np.mean(np.log(group['Overhead Single'])))
        
        geomean_rows.append({
            'Benchmark': 'Geomean',
            'Size': size,
            'Overhead Constantine': geo_const,
            'Overhead Vector': geo_vector,
            'Overhead Single': geo_single
        })

    result_df = pd.concat([result_df, pd.DataFrame(geomean_rows)], ignore_index=True)

    result_df['Vector Reduction (%)'] = (result_df['Overhead Constantine'] - result_df['Overhead Vector']) / result_df['Overhead Constantine'] * 100
    result_df['Single Reduction (%)'] = (result_df['Overhead Constantine'] - result_df['Overhead Single']) / result_df['Overhead Constantine'] * 100

    display_df = result_df.copy()
    display_df['Method'] = display_df['Size'].apply(lambda x: 'Gather/Scatter-based' if x == 64 else 'Packed Load/Store-based')

    cols_oh = ['Overhead Constantine', 'Overhead Vector', 'Overhead Single']
    for col in cols_oh:
        display_df[col] = display_df[col].apply(lambda x: f"{x:.1f}x")

    cols_red = ['Vector Reduction (%)', 'Single Reduction (%)']
    for col in cols_red:
        display_df[col] = display_df[col].apply(lambda x: f"{x:.1f}%")

    display_df.sort_values(by=['Size', 'Benchmark'], key=lambda col: col.map(lambda x: 'zzzz' if x=='Geomean' else x), inplace=True)

    md_output = manual_markdown_table(display_df[['Method', 'Benchmark', 'Overhead Constantine', 'Single Reduction (%)', 'Vector Reduction (%)']])

    print("# Final Results\n")
    print(md_output)
    with open(output_md_path, 'w', encoding='utf-8') as f:
        f.write("## Results for real-world applications (Section 6.2)\n\n")
        f.write(md_output)

    print(f"Output: {output_md_path}")

if __name__ == "__main__":
    process_benchmark_data(
        file1_path='/app/src/real-world-apps/stats_summary.csv', 
        file2_path='/app/src_constantine+/real-world-apps/stats_summary.csv', 
        output_md_path='results.md'
    )
