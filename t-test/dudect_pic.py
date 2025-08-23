import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os
import matplotlib as mpl
from matplotlib.patches import Rectangle

mpl.rcParams['font.size'] = 7
mpl.rcParams['font.family'] = 'sans-serif'
# mpl.rcParams['font.serif'] = ['CMU Serif', 'DejaVu Serif'] # 'Times New Roman', 
mpl.rcParams['pdf.fonttype'] = 42 
mpl.rcParams['axes.labelpad'] = 0  
mpl.rcParams['xtick.major.pad'] = 0 
mpl.rcParams['ytick.major.pad'] = 0 
mpl.rcParams['legend.fontsize'] = 8


def generate_group_heatmaps(csv_path, output_dir,
                                group_col, x_col, y_col,
                                value_col1, value_col2,
                                highlight_regions=None,
                                figsize=(3.25, 3), cmap='viridis'):
    """
    For each group in 'group_col', generate two heatmaps over a square grid of all masks:
      - One for abs(value_col1)
      - One for value_col2
    Both (x_col, y_col) and (y_col, x_col) entries are included to symmetrize the matrix.
    """
    df = pd.read_csv(csv_path)
    df.columns = df.columns.str.strip()
    os.makedirs(output_dir, exist_ok=True)

    abs1_vmin = df[value_col1].abs().min()
    abs1_vmax = df[value_col1].abs().max()
    v2_vmin = 2 # df[value_col2].min()
    v2_vmax = 0 # df[value_col2].max()
    
    for grp in df[group_col].unique():
        subset = df[df[group_col] == grp]
        # Collect all unique mask values from x_col and y_col
        unique_masks = list(pd.unique(subset[[x_col, y_col]].values.ravel())[::-1])
        mask_labels = [f'0x{m.replace("Mask ", "").upper():0>4}' for m in unique_masks]
        
        # Initialize empty DataFrames for both metrics
        mat1 = pd.DataFrame(np.nan, index=unique_masks, columns=unique_masks)
        mat2 = pd.DataFrame(np.nan, index=unique_masks, columns=unique_masks)
        
        # Fill matrices
        for _, row in subset.iterrows():
            m1 = row[x_col]
            m2 = row[y_col]
            v1 = abs(row[value_col1])
            v2 = row[value_col2]
            mat1.at[m1, m2] = v1
            mat1.at[m2, m1] = v1
            mat2.at[m1, m2] = v2
            mat2.at[m2, m1] = v2
        
        # Optionally fill diagonal with zeros or NaN; keep NaN for clarity
        
        for mat, label in [(mat1, f"abs_{value_col1}"), (mat2, value_col2)]:
            fig, ax = plt.subplots(figsize=figsize)
            cax = ax.imshow(mat.values, aspect='auto', origin='lower',
                            interpolation='nearest', cmap=cmap,
                            vmin=abs1_vmin if label.startswith('abs_') else v2_vmin,
                            vmax=abs1_vmax if label.startswith('abs_') else v2_vmax)
            ax.set_xticks(np.arange(len(unique_masks)))
            ax.set_xticklabels(mask_labels, rotation=45, ha='right')
            ax.set_yticks(np.arange(len(unique_masks)))
            ax.set_yticklabels(mask_labels)

            # optional highlights
            if highlight_regions is not None:
                for (x0_label, y0_label, x1_label, y1_label, rect_kwargs) in highlight_regions:
                    x0 = mask_labels.index(x0_label)
                    y0 = mask_labels.index(y0_label)
                    x1 = mask_labels.index(x1_label)
                    y1 = mask_labels.index(y1_label)
                    width = x1 - x0 + 1
                    height = y1 - y0 + 1
                    rect = Rectangle((x0 - 0.5, y0 - 0.5), width, height, 
                                     fill=False, **rect_kwargs)
                    ax.add_patch(rect)
                    # color = rect_kwargs.get('edgecolor', 'red')
                    # lw = rect_kwargs.get('linewidth', 2)
                    # ax.plot([x0-0.5, x1+0.5], [y1+0.5, y1+0.5], color=color, linewidth=lw)
                    # ax.plot([x1+0.5, x1+0.5], [y0-0.5, y1+0.5], color=color, linewidth=lw)

            ax.set_xlabel("Mask x")
            ax.set_ylabel("Mask y")
            # ax.set_title(f"{grp} : {label}", fontsize=7)
            if label.startswith('abs_'):
                colorbar_label = "t-statistic"
            else:
                colorbar_label = "Difference Detected"
            fig.colorbar(cax, ax=ax, label=colorbar_label)
            fig.tight_layout(pad=0)
            safe_grp = str(grp).replace(' ', '_').replace('/', '_')
            out_file = os.path.join(output_dir, f"heatmap_{safe_grp}_{label}.pdf")
            fig.savefig(out_file)
            plt.close(fig)
            print(f"Saved: {out_file}")


highlights = [('0x8000', '0x8000', '0x0100', '0x0100', 
         {'edgecolor':'red','linewidth':2}),
         ('0xA000', '0xA000', '0x6000', '0x6000', 
         {'edgecolor':'red','linewidth':2}),
         ('0x4444', '0x4444', '0x5500', '0x5500', 
         {'edgecolor':'red','linewidth':2}),
         ('0x5999', '0x5999', '0x5555', '0x5555', 
         {'edgecolor':'red','linewidth':2})]

root_path = "data/"
tests = ['packed_singleline_time', 
         'gather_scatter_singleline_time', 
         'gather_scatter_multiline_time',
         'packed_load_store_false_dependency']

for i in tests: 
    generate_group_heatmaps(
        csv_path=root_path + i + '.csv_dudect_results.csv',
        output_dir=root_path + i + '_heatmaps/',
        group_col="group",
        x_col="mask 1",
        y_col="mask 2",
        value_col1="t_value",
        value_col2="difference_detected",
        highlight_regions=None if 'false_dependency' in i else highlights,
    )


# | :--------: | :------------------ | :- |
# |    ffff    | 1111 1111 1111 1111 | 16 |
# |    a000    | 1010 0000 0000 0000 | 16 |
# |    8000    | 1000 0000 0000 0000 | 16 |
# |    6000    | 0110 0000 0000 0000 | 16 |
# |    5999    | 0101 1001 1001 1001 | 16 |
# |    5555    | 0101 0101 0101 0101 | 16 |
# |    5500    | 0101 0101 0000 0000 | 16 |
# |    4444    | 0100 0100 0100 0100 | 16 |
# |    4204    | 0100 0010 0000 0100 | 16 |
# |    4000    | 0100 0000 0000 0000 | 16 |
# |     200    | 0000 0010 0000 0000 | 16 |
# |     100    | 0000 0001 0000 0000 | 16 |
# |      0     | 0000 0000 0000 0000 | 16 |
