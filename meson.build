project('ZNWorkload', 'c')

subprojects = [
    'zone-evict-ideal',
    'zone-evict-unideal',
]

# define zbd dep
zbd_lib = declare_dependency(
    include_directories: include_directories('vendor/lib/include'),
    link_args: ['-L../vendor/lib/lib', '-lzbd', '-Wl,-rpath,' + meson.current_source_dir() / 'vendor/lib/lib']
)

meson.override_dependency('zbd', zbd_lib)

# Trigger compilation for each subproject
foreach sp : subprojects
    sub = subproject(sp, default_options: ['default_library=static'])
endforeach