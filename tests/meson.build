project_tests = [
    'minheap', 'minheap_concurrent'
]

foreach test_name : project_tests
    src= files(
        meson.source_root() + '/src/znutil.c',
        meson.source_root() + '/src/cachemap.c',
        meson.source_root() + '/src/zone_state_manager.c',
        meson.source_root() + '/src/eviction_policy.c',
        meson.source_root() + '/src/minheap.c',
        meson.source_root() + '/src/eviction/promotional.c',
        meson.source_root() + '/src/eviction/chunk.c',
        meson.source_root() + '/src/eviction/zone.c',
        test_name + '.c')
    test_exe = executable(test_name, src,
                          include_directories : inc_dir,
                          c_args : cflags,
                          dependencies : [ zbd_lib, dependency('glib-2.0') ],
                          install: false)
    test(test_name, test_exe)
endforeach
