script_files = [
    'bdecode',
    'rsyncinfo',
    'rsyncusers',
    'create_timestamp',
    'null-rsync',
    'scanner.pl',
]

logrotate_files = [
    'mirrorprobe-logrotate',
]


install_data(script_files,
    install_dir: get_option('bindir'),
    install_mode: ['rwxr-xr-x'],
)

install_data(logrotate_files,
    install_dir: '/etc/logrotate.d',
    install_mode: ['rw-r--r--'],
    rename: ['mirrorprobe'],
)