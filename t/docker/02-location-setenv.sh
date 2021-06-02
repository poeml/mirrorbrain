#!lib/test-in-container-systemd.sh lib/init/3-local-mirrors-location.sh

set -ex
source lib/common.sh

sudo zypper -n install apache2-mod_security2
systemctl stop apache2
a2enmod unique_id
a2enmod security2
sed -i '/ServerSignature On/a SecRuleEngine On\nSecRule ARGS:mmdb_addr . "phase:1,t:none,id:32768,log,setenv:'"'"'MMDB_ADDR=%{MATCHED_VAR}'"'" /etc/apache2/vhosts.d/MAIN.conf
systemctl start apache2

curl --interface 127.0.0.3 127.0.0.1:80/downloads/folder1/file1.dat

tail /var/log/apache2/MAIN-error_log

grep 'Chose server mirrorEU' /var/log/apache2/MAIN-error_log
grep -qv 'Chose server mirrorNA' /var/log/apache2/MAIN-error_log
grep -qv 'Chose server mirrorAS' /var/log/apache2/MAIN-error_log

curl --interface 127.0.0.2 127.0.0.1:80/downloads/folder1/file1.dat
tail /var/log/apache2/MAIN-error_log
grep 'Chose server mirrorNA' /var/log/apache2/MAIN-error_log

# make sure client address is rewritten with GET parameters according to 
curl --interface 127.0.0.2 127.0.0.1:80/downloads/folder1/file1.dat?mmdb_addr=127.0.0.4
tail /var/log/apache2/MAIN-error_log
grep 'clientip: 127.0.0.4' /var/log/apache2/MAIN-error_log
grep 'Chose server mirrorAS' /var/log/apache2/MAIN-error_log
