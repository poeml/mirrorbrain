# TODO: we dont have any egg info file. so no pip integration
# TODO: patch mb/__init__.py based on meson.project_version()

python3 = import('python').find_installation('python3', required: true)
python3_dep = dependency('python3')

python3.extension_module('zsync',
    ['zsyncmodule.c'],
    install: true,
    dependencies: [python3_dep],
)

mb_script_files = [
    'scripts/mb',
    'scripts/mirrorprobe',
]

python_module_dir = python3.get_install_dir(pure: true)
install_subdir(['mb'],
    install_dir: python_module_dir,
    install_mode: ['rw-r--r--'],
)

install_data(mb_script_files,
    install_dir: get_option('bindir'),
    install_mode: ['rwxr-xr-x'],
)

if get_option('install-icons')
    install_subdir(['famfamfam_flag_icons'],
    install_dir: get_option('datadir') + '/' + meson.project_name(),
    install_mode: ['rw-r--r--'],
    exclude_files: ['famfamfam_flag_icons/readme.txt'],
    )
endif
