prog_python = import('python').find_installation('python3')
# TODO: install flag files
install_subdir(['famfamfam_flag_icons'],
  install_dir: get_option('datadir') + '/' + meson.project_name(),
  install_mode: ['rw-r--r--', 'root', 'root'],
  exclude_files: ['famfamfam_flag_icons/readme.txt'],
)