mod_mirrorbrain = shared_module('mod_autoindex_mb',
    ['mod_autoindex_mb.c'],
    dependencies: [lib_m, apr],
    build_rpath: apache_libexecdir,
    include_directories: [include_directories(apache_includedir)],
    name_prefix: '',
    install: true,
    install_dir: apache_libexecdir,
)
