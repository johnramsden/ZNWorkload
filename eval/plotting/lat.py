#!/usr/bin/env python3
import argparse
import statistics
import numpy as np

import csv
import matplotlib.pyplot as plt

def main():
    parser = argparse.ArgumentParser(
        description="Scatter plot CSV data. Uses the second column as the default y-axis label and plot title."
    )
    parser.add_argument("data_file", help="Path to the CSV file.")
    parser.add_argument(
        "--yaxis",
        help="Label for the y-axis. Defaults to the value in the second column.",
        default=None
    )
    parser.add_argument(
        "--title",
        help="Title for the plot. Defaults to the value in the second column.",
        default=None
    )
    parser.add_argument(
        "--output",
        help="Output file.",
        default=None
    )
    parser.add_argument(
        "--regression",
        action="store_true",
        help="Add regression line to plot."
    )
    parser.add_argument(
        "--type",
        help="plot type.",
        choices=['scatter', 'line'],
        default="scatter"
    )
    args = parser.parse_args()

    x_vals = []
    y_vals = []
    default_label = None

    # Read the CSV file and extract data.
    with open(args.data_file, 'r') as f:
        reader = csv.reader(f)
        for row in reader:
            # Set the default label from the middle column of the first row.
            if default_label is None:
                default_label = row[1]
            try:
                # Convert time from ms to minutes.
                x_val = float(row[0]) / 60000.0
                y_val = float(row[2]) / 1_000_000
                if y_val == 0.0:
                    continue
            except ValueError as e:
                print(f"Skipping row {row}: {e}")
                continue
            x_vals.append(x_val)
            y_vals.append(y_val)

    print(f"Average: {statistics.fmean(y_vals)}")

    # Use the command-line values if provided; otherwise default to the middle column value.
    y_label = args.yaxis if args.yaxis is not None else default_label
    plot_title = args.title if args.title is not None else default_label

    # Create scatter plot
    plt.figure(figsize=(12, 6))
    if args.type == "scatter":
        plt.scatter(x_vals, y_vals, label=default_label, s=1)
    else:
        plt.plot(x_vals, y_vals, label=default_label)

    if args.regression:
        x = np.array(x_vals)
        y = np.array(y_vals)

        slope, intercept = np.polyfit(x_vals, y, 1)
        y_fit = slope * x + intercept

        # Plot the linear regression line
        plt.plot(x, y_fit, color='red')

    # Disable scientific notation on the y-axis
    plt.ticklabel_format(style='plain', axis='y')
    plt.xlabel("Time (minutes)")
    plt.ylabel(y_label)
    plt.title(plot_title)
    plt.legend()

    out = f"data/{plot_title}.png"
    if args.output is not None:
        out = args.output
    plt.savefig(out)
    # plt.show()

if __name__ == '__main__':
    main()
