# Architecture

## Eviction

### Chunk LRU

* `chunk_to_lru_map`: Map chunk to location in LRU queue
* `lru_queue`: LRU of chunks
* `zone_pool`: List for each zone containing:
  * `chunks_in_use`: zone in use counter
  * `chunks`: Backing array for LRU
  * `pqueue_entry`: Entry in `pqueue`, or `NULL`
* `invalid_pqueue`: Priority queue keeping track of invalid zones (`chunks_invalid`)

**Summary:**

On init:
Setup backing `zone_pool`, contains list of chunks set `!in_use`, and `chunks_valid=0`

On update:
* Update `chunk_to_lru_map`, `lru_queue`, `chunks`
* If fills, add to `invalid_pqueue`. In pqueue, `data=&chunk`.
* If in `pqueue`, call update minheap

On evict:
* Evict based on LRU
* Update `chunks_in_use`, `chunks`

On GC:
* Pop from `invalid_pqueue`, migrate via:
  * grab zones from active queues, write until all valid written and low water thresh met
  * If SSD update zsm invalid

Issues:
* GC on SSD - skip

#### Eviction

#### GC