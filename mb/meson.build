prog_python = import('python').find_installation('python3')

if get_option('install-icons')
    install_subdir(['famfamfam_flag_icons'],
    install_dir: get_option('datadir') + '/' + meson.project_name(),
    install_mode: ['rw-r--r--'],
    exclude_files: ['famfamfam_flag_icons/readme.txt'],
    )
endif