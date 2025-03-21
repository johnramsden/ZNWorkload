#!/usr/bin/env python3
import argparse
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
                y_val = float(row[2])
            except ValueError as e:
                print(f"Skipping row {row}: {e}")
                continue
            x_vals.append(x_val)
            y_vals.append(y_val)

    # Use the command-line values if provided; otherwise default to the middle column value.
    y_label = args.yaxis if args.yaxis is not None else default_label
    plot_title = args.title if args.title is not None else default_label

    # Create scatter plot.
    plt.scatter(x_vals, y_vals, label=default_label)
    plt.xlabel("Time (minutes)")
    plt.ylabel(y_label)
    plt.title(plot_title)
    plt.legend()
    plt.savefig(f"{plot_title}.png")

if __name__ == '__main__':
    main()
