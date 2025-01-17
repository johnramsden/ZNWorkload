# ZNWorkload

Test workloads for ZNS

## Setup

Clone repo:

```shell
git clone https://github.com/johnramsden/ZNWorkload.git --recursive
```

Compile dependencies:

```shell
./build-deps.sh
```

Compile project:

```shell
meson setup buildDir
meson compile -C buildDir
```

## Testing and Development

Create an emulated ZNS device via `scripts/nullblk-zones.sh` with:

* sector size: 4096B
* zone size: 32MiB
* conventional zones (non seq writes): 0
* sequential zones: 100

```shell
./scripts/nullblk-zoned.sh 4096 32 0 100
```

```
Created /dev/nullb0
```

To destroy:

```shell
./scripts/nullblk-zoned-delete.sh 0 # Replace 0 with ID if different
```

### Min-workload

For mini-test:

* 14 zones
* 8chunks per zone
* `2*14=28 is capacity`
* Start evict at 2 zones free: `24` entries
* On evict, evict 4 zones: `20` entries remain

```shell
./scripts/nullblk-zoned.sh 4096 1 0 14
```

```shell
./ze_cache /dev/nullb0 524288
```

This means 2chunks to fill a zone: `1024*1024/2`