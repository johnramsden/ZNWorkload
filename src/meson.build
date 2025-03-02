# Get the sources
srcs = files('zncache.c', 'znutil.c')

executable(
    'zncache',
    srcs,
    include_directories : inc_dir,
    c_args : cflags,
    dependencies: [zbd_lib, dependency('glib-2.0')]
)