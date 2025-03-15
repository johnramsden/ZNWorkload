project('ZNWorkload', 'c',
        version : '1.0.0',
        default_options : [
            'warning_level=3',
            'c_std=c17',
            # To test ASAN
            #                'b_sanitize=address'
        ])

# define zbd dep
zbd_lib = declare_dependency(
    include_directories: include_directories('vendor/lib/include'),
    link_args: ['-L../vendor/lib/lib', '-lzbd', '-Wl,-rpath,' + meson.current_source_dir() / 'vendor/lib/lib']
)

# Read options
verify_enabled = get_option('verify')
debug_enabled = get_option('debugging')
BLOCK_ZONE_CAPACITY = get_option('BLOCK_ZONE_CAPACITY')

# Conditional compiler flags
cflags = ['-DBLOCK_ZONE_CAPACITY=' + BLOCK_ZONE_CAPACITY.to_string()]
if verify_enabled
    cflags += ['-DVERIFY']
endif

if debug_enabled
    cflags += ['-DDEBUG', '-g']
endif

# Print options for debugging purposes
message('Verify mode: ' + verify_enabled.to_string())
message('Debug mode: ' + debug_enabled.to_string())

# Define the include directory
inc_dir = include_directories('include')

# Add the src subdirectory
subdir('src')
subdir('tests')
