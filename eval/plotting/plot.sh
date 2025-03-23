#!/bin/sh

CSV_DIR=$1

python bytes.py "${CSV_DIR}/CACHE_USED_MIB.csv" \
    --yunits "GiB" \
    --inunits "MiB" \
    --title "Cache Used" \
    --type "line" \
    --yaxis "Cache Used (GiB)"

python bytes.py "${CSV_DIR}/CACHETHROUGHPUT.csv" \
    --yunits "GiB" \
    --inunits "B" \
    --title "Cache Throughput" \
    --type "line" \
    --yaxis "Cache Throughput (GiB/s)"

python bytes.py "${CSV_DIR}/CACHEMISSTHROUGHPUT.csv" \
    --yunits "GiB" \
    --inunits "B" \
    --title "Cache Miss Throughput" \
    --type "line" \
    --yaxis "Cache Miss Throughput (GiB/s)"

python bytes.py "${CSV_DIR}/CACHEHITTHROUGHPUT.csv" \
    --yunits "GiB" \
    --inunits "B" \
    --title "Cache Hit Throughput" \
    --type "line" \
    --yaxis "Cache Hit Throughput (GiB/s)"

python lat.py "${CSV_DIR}/CACHEHITLATENCY.csv" \
    --title "Cache Hit Latency" \
    --type "line" \
    --yaxis "Hit Latency (ms)"

python lat.py "${CSV_DIR}/CACHEMISSLATENCY.csv" \
    --title "Cache Miss Latency" \
    --type "line" \
    --yaxis "Miss Latency (ms)"

python lat.py "${CSV_DIR}/GETLATENCY.csv" \
    --title "Cache Get Latency" \
    --type "line" \
    --yaxis "Get Latency (ms)"

python lat.py "${CSV_DIR}/GETLATENCY_EVERY.csv" \
    --title "Cache Get Latency (Individual)" \
    --type "scatter" \
    --yaxis "Get Latency (ms)"

python lat.py "${CSV_DIR}/READLATENCY.csv" \
    --title "Cache Read Latency" \
    --type "line" \
    --yaxis "Read Latency (ms)"

python lat.py "${CSV_DIR}/READLATENCY_EVERY.csv" \
    --title "Cache Read Latency (Individual)" \
    --type "scatter" \
    --yaxis "Read Latency (ms)"

python lat.py "${CSV_DIR}/WRITELATENCY.csv" \
    --title "Cache Write Latency" \
    --type "line" \
    --yaxis "Write Latency (ms)"

python lat.py "${CSV_DIR}/WRITELATENCY_EVERY.csv" \
    --title "Cache Write Latency (Individual)" \
    --type "scatter" \
    --yaxis "Write Latency (ms)"

python plot.py "${CSV_DIR}/FREEZONES.csv" \
    --title "Free Zones" \
    --type "line" \
    --yaxis "Free Zones"

python plot.py "${CSV_DIR}/HITRATIO.csv" \
    --title "Hitratio" \
    --type "line" \
    --yaxis "Hit-ratio"
