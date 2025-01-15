project('zone-evict-ideal', 'c',
        version : '1.0.0',
        default_options : ['warning_level=3', 'c_std=c2x', 'b_sanitize=address'])

executable(
        'zone-evict-ideal',
        'zone-evict-ideal.c',
        dependencies: [dependency('zbd')]
)