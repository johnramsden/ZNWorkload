project_tests = [
    'minheap', 'minheap_concurrent', 'chunk_eviction'
]

test_cflags = ['-DBLOCK_ZONE_CAPACITY=' + BLOCK_ZONE_CAPACITY.to_string()]

foreach test_name : project_tests
    src = files(
        meson.project_source_root() + '/src/cache.c',
        meson.project_source_root() + '/src/znutil.c',
        meson.project_source_root() + '/src/cachemap.c',
        meson.project_source_root() + '/src/zone_state_manager.c',
        meson.project_source_root() + '/src/eviction_policy.c',
        meson.project_source_root() + '/src/minheap.c',
        meson.project_source_root() + '/src/eviction/promotional.c',
        meson.project_source_root() + '/src/eviction/chunk.c',
        test_name + '.c'
    )
    test_exe = executable(test_name, src,
                          include_directories : inc_dir,
                          c_args : test_cflags,
                          dependencies : [ zbd_lib, dependency('glib-2.0') ],
                          install: true)
    test(test_name, test_exe)
endforeach
