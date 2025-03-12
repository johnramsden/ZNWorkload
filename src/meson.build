#Get the sources
srcs = files(
    'zncache.c',
    'cache.c',
    'znutil.c',
    'cachemap.c',
    'zone_state_manager.c',
    'eviction_policy.c',
    'minheap.c',
    'eviction/promotional.c',
    'eviction/chunk.c',
    'eviction/zone.c'
)

executable('zncache',
           srcs,
           include_directories : inc_dir,
           c_args : cflags,
           dependencies : [ zbd_lib, dependency('glib-2.0') ]
)