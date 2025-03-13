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
* Determine if evict based on high chunk thresh
* if `total_chunks-len(lru_queue)` < high chunk thresh, evict
* Invalidation:
  * Update `chunks_in_use`, `chunks`
  * Update minheap
  * Update Invalid queue in ZSM
  * Update cachemap (`zn_cachemap_clear_chunk`)
  * Update ZSM (`zsm_mark_chunk_invalid`)

On GC:
* Pop from `invalid_pqueue`, migrate via:
  * read in old data
  * grab active zone
  * write out old data
  * Update mappings

Issues
  * What if none avail?
    * Add extra GC only zone?
  * Do we buffer into RAM then send all at once to disk?
  * Alternative -> read chunk, write chunk, downside, requires extra zone

Issues:
* GC on SSD - skip, instead use `zn_zone` invalid queue
  * set `filled=true`
  * Put zn_zone back in active, find `chunk_offset` in `invalid` queue

#### Eviction

#### GC