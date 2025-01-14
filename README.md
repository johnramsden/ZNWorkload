# ZNWorkload

Test workloads for ZNS

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