#!/bin/bash
# shellcheck disable=SC2154

set -e

echo "Device: $1"
echo "Workload Directory: $2"
echo "Threads: $3"
echo "Logging to: ./logs"

# Specify the directory (use . for the current directory)
directory="$2"
threads="$3"

usage() {
    printf "Usage: %s DEVICE WORKLOAD_DIR NR_THREADS\n" "$(basename "$0")"
    exit 1
}

if [ "$#" -ne 3 ]; then
    echo "Illegal number of parameters $#, should be 3"
    usage
fi

# Check set
[ -z $1 ] && usage
[ -z $2 ] && usage
[ -z $3 ] && usage

ret=0

# Loop through all files in the directory
for file in "$directory"/*.bin; do
    # Check if it's a file (not a directory)
    if ! [ -f "$file" ]; then
        echo "ERROR: no such file '$file', skipping"
        continue
    fi
    filename=$(basename ${file})
    # Split the filename into an array using comma as the delimiter
    IFS=, read -ra params <<< "${filename%.*}"

    # Iterate over each key-value pair
    for param in "${params[@]}"; do
        # Extract the key and value by removing the longest matching suffix and prefix respectively
        key="${param%%=*}"
        value="${param#*=}"
        # Declare the variable globally in the script
        declare -g "$key"="$value"
    done

    runfile="./logs/$(date '+%Y-%m-%d_%H:%M:%S')-run"
    # Now you can access the variables
    {
        echo "Chunk Size: $chunk_size"
        echo "Latency: $latency"
        echo "Distribution Type: $distr"
        echo "Working Set Ratio: $ratio"
        echo "Zone Size: $zone_size"
        echo "Iterations: $iterations"
        echo "Number of Zones: $n_zones"
        echo "Total Chunks: $chunks"
        echo "High Water: $evict_high"
        echo "Low water: $evict_low"
        echo "Eviction $eviction"

    } | tee "$runfile"

    echo >> "$runfile"

    # shellcheck disable=SC2024
    echo "Running $runfile"

    meson setup --reconfigure buildDir -Dverify=false \
                                       -Ddebugging=false \
                                       -DREAD_SLEEP_US="$latency" \
                                       -DEVICT_HIGH_THRESH_ZONES="$evict_high" \
                                       -DEVICT_LOW_THRESH_ZONES="$evict_low" \
                                       -DEVICT_HIGH_THRESH_CHUNKS="$evict_high" \
                                       -DEVICT_LOW_THRESH_CHUNKS="$evict_low" \
                                       -DEVICTION_POLICY="$eviction" \
                                       -DPROFILING_INTERVAL_SEC="60" \
                                        >/dev/null
    meson compile -C buildDir >/dev/null

    # shellcheck disable=SC2024
    if ! sudo ./buildDir/src/zncache "$1" "$chunk_size" "$threads" -w "$file" -i "$iterations" -m "$runfile.profile.csv" >> "$runfile"; then
        echo "Run FAILED!"
        ret=1
    else
        tail -n3 "$runfile"
    fi
done

exit $ret