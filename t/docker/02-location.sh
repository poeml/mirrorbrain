#!lib/test-in-container-systemd.sh lib/init/3-local-mirrors-location.sh

set -ex
source lib/common.sh

curl --interface 127.0.0.3 127.0.0.1:80/downloads/folder1/file1.dat

tail /var/log/apache2/MAIN-error_log

grep 'Chose server mirrorEU' /var/log/apache2/MAIN-error_log
grep -qv 'Chose server mirrorNA' /var/log/apache2/MAIN-error_log
grep -qv 'Chose server mirrorAS' /var/log/apache2/MAIN-error_log

curl --interface 127.0.0.2 127.0.0.1:80/downloads/folder1/file1.dat
tail /var/log/apache2/MAIN-error_log
grep 'Chose server mirrorNA' /var/log/apache2/MAIN-error_log
