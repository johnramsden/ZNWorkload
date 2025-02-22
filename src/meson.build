# Get the sources
srcs = files('ze_cache.c', 'ze_rw_lock.c')

executable(
    'ze_cache',
    srcs,
    include_directories : inc_dir,
    c_args : cflags,
    dependencies: [zbd_lib, dependency('glib-2.0')]
)