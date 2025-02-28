# Get the sources
srcs = files('zncache.c', 'util.c', 'cachemap.c', 'eviction_policy_mgr.c', 'zone_state_mgr.c')

executable(
    'zncache',
    srcs,
    include_directories : inc_dir,
    c_args : cflags,
    dependencies: [zbd_lib, dependency('glib-2.0')]
)