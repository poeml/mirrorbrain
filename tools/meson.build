script_files = [
    'scanner.pl',
]

extra_script_files = [
    'bdecode',
    'rsyncinfo',
    'rsyncusers',
    'create_timestamp',
    'null-rsync',
]

logrotate_files = [
    'mirrorprobe-logrotate',
]


install_data(script_files,
    install_dir: get_option('bindir'),
    install_mode: ['rwxr-xr-x'],
    rename: ['scanner'],
)

if get_option('tools')
install_data(extra_script_files,
    install_dir: get_option('bindir'),
    install_mode: ['rwxr-xr-x'],
)
endif

install_data(logrotate_files,
    install_dir: '/etc/logrotate.d',
    install_mode: ['rw-r--r--'],
    rename: ['mirrorprobe'],
)
