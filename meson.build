project('hypersonik', 'c')

add_global_arguments(
    '-DCOBJMACROS',
    '-ffunction-sections',
    '-fdata-sections',
    language: 'c',
)

add_global_link_arguments(
    '-Wl,--gc-sections',
    '-mwindows',
    '-static-libgcc',
    language: 'c',
)

cc = meson.get_compiler('c')
lib_avrt = cc.find_library('avrt')

subdir('src')
