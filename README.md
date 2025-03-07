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

There are two variables that can be set:

* `DEBUG`: Enables debug output (default true)
* `VERIFY`: Enables correctness verification (default true)

To modify these:

```shell
meson setup --reconfigure buildDir -Dverify=false -Ddebugging=false
meson compile -C buildDir
```

## Testing and Development

Create an emulated ZNS device via `scripts/nullblk-zones.sh` with:

* sector size: 4096B
* zone size: 32MiB
* conventional zones (non seq writes): 0
* sequential zones: 100

```shell
./scripts/nullblk.sh 4096 32 0 100 "zns" # or ssd
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

ZNS:

```shell
./scripts/nullblk.sh 4096 1 0 14 "zns"
```

On SSD:

```shell
./scripts/nullblk.sh 4096 1 0 14 "ssd"
```

```shell
./zncache /dev/nullb0 524288 2
```

This means 2chunks to fill a zone: `1024*1024/2`

### Documentation

Run `doxygen`:

```shell
doxygen Doxyfile
```

Open `docs/html/index.html` in a browser.

### Formatting

Run `clang-format`:

```shell
clang-format -i src/ze_cache.c
```