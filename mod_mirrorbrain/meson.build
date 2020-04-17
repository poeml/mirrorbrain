lib_m = meson.get_compiler('c').find_library('m', required: true)

if get_option('memcached')
    add_project_arguments('-DWITH_MEMCACHED', language:'c')
endif

add_project_arguments('-DMOD_MIRRORBRAIN_VER="' + meson.project_version() + '"', language: 'c')

mod_mirrorbrain = shared_module('mod_mirrorbrain',
    ['mod_mirrorbrain.c'],
    dependencies: [lib_m, apr],
    build_rpath: apache_libexecdir,
    include_directories: [apache_includedir],
    name_prefix: '',
    install: true,
    install_dir: apache_libexecdir,
)
