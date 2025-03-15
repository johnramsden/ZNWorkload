#!/bin/bash

echo "Device: $1"
echo "Workload Directory: $2"
echo "Threads: $3"

# Specify the directory (use . for the current directory)
directory="$2"
threads="$3"

# Loop through all files in the directory
for file in "$directory"/*; do
    # Check if it's a file (not a directory)
    if ! [ -f "$file" ]; then
        echo "ERROR: no such file '$file', skipping"
        continue
    fi
    filename=$(basename ${file})
    # Split the filename into an array using comma as the delimiter
    IFS=, read -ra params <<< "$filename"

    # Iterate over each key-value pair
    for param in "${params[@]}"; do
        # Extract the key and value by removing the longest matching suffix and prefix respectively
        key="${param%%=*}"
        value="${param#*=}"
        # Declare the variable globally in the script
        declare -g "$key"="$value"
    done

    runfile="$file""-"$(date '+%Y-%m-%d_%H:%M:%S')"-run"
    # Now you can access the variables
    {
        echo "Chunk Size: $chunk_size"
        echo "Distribution Type: $distributionType"
        echo "Working Set Ratio: $working_set_ratio"
        echo "Zone Size: $zone_size"
        echo "Iterations: $iterations"
        echo "Number of Zones: $num_zones"
        echo "Total Chunks: $total_chunks"
    } >> "$runfile"

    # shellcheck disable=SC2024
    if ! sudo ./buildDir/src/zncache "$1" "$chunk_size" "$threads" "$file" "$iterations" >> "$runfile"; then
        echo "Run FAILED!"
    fi
done

