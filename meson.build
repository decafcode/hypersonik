project('hypersonik', 'c', version: '0.1.1')

add_global_arguments(
    '-Wall',
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

if get_option('buildtype') == 'release'
    add_global_arguments('-DNDEBUG', language: 'c')
endif

cc = meson.get_compiler('c')
lib_avrt = cc.find_library('avrt')
lib_msacm32 = cc.find_library('msacm32')

subdir('src')
