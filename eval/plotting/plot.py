#!/usr/bin/env python3
import sys
import pandas as pd
import matplotlib.pyplot as plt

def main():
    if len(sys.argv) != 3:
        print("Usage: python plot.py <title> <csv_file>")
        sys.exit(1)

    title = sys.argv[1]
    csv_file = sys.argv[2]

    # Read the CSV file without a header
    try:
        data = pd.read_csv(csv_file, header=None)
    except Exception as e:
        print(f"Error reading {csv_file}: {e}")
        sys.exit(1)

    # Check if the second column exists
    if data.shape[1] < 2:
        print("CSV file must have at least two columns")
        sys.exit(1)

    # Extract the second column (index 1) as the values to plot
    values = data[1]

    # Create a dot plot using a scatter plot where the x-axis is the index of each value.
    plt.figure(figsize=(10, 6))
    plt.scatter(range(len(values)), values, marker='o')
    plt.title(title)
    plt.xlabel("Index")
    plt.ylabel("Value")
    plt.grid(True)
    plt.tight_layout()
    plt.show()

if __name__ == '__main__':
    main()
