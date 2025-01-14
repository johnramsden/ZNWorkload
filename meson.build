project('ZNWorkload', 'c')

subprojects = [
    'zone-evict-ideal',
    'zone-evict-unideal',
]

# Trigger compilation for each subproject
foreach sp : subprojects
    sub = subproject(sp, default_options: ['default_library=static'])
endforeach