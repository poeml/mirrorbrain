#!lib/test-in-container-systemd.sh

set -ex
source lib/common.sh

curl -v 127.0.0.1:80/downloads/folder1/file1.dat

tail /var/log/apache2/MAIN-error_log

grep 'Chose server mirror' /var/log/apache2/MAIN-error_log

