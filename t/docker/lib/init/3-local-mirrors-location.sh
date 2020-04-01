thisdir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

. $thisdir/install-from-source.sh

setup_vhost1() {
    local site=$1 
    local offset=$2
    local continent=$3
    local country=$4
    cp /etc/apache2/vhosts.d/vhost.template /etc/apache2/vhosts.d/$site.conf
    sed -i "s,dummy-host.example.com,$site," /etc/apache2/vhosts.d/$site.conf
    sed -i "s,127.0.0.1,127.0.0.$offset," /etc/apache2/vhosts.d/$site.conf
    if [ "$port" == 80 ]; then
        sed -i "/ServerSignature On/a DBDriver pgsql\nDBDParams \"host=localhost user=mirrorbrain password=mirrorbrain dbname=mirrorbrain connect_timeout=15\"\nMirrorBrainMetalinkPublisher '$site' http://127.0.0.1" /etc/apache2/vhosts.d/$site.conf
        sed -i '/Directory "\/srv\/www\/vhosts\/MAIN"/a MirrorBrainEngine On\n        MirrorBrainDebug On\n        FormGET On\n        MirrorBrainHandleHEADRequestLocally Off\n        MirrorBrainMinSize 0\n        MirrorBrainExcludeUserAgent rpm\/4.4.2*\n        MirrorBrainExcludeUserAgent *APT-HTTP*\n        MirrorBrainExcludeMimeType application\/pgp-keys' /etc/apache2/vhosts.d/$site.conf
        mkdir -p /srv/hashes/srv/www/vhosts/$site/downloads/
        chown -R mirrorbrain:mirrorbrain srv/hashes/srv/www/vhosts/$site/downloads/
    fi

    mkdir -p /srv/www/vhosts/$site/downloads/{folder1,folder2,folder3}
    echo /srv/www/vhosts/$site/downloads/{folder1,folder2,folder3}/{file1,file2}.dat | xargs -n 1 touch
}

setup_vhost1 mirrorNA 2 NA us
setup_vhost1 mirrorEU 3 EU de
setup_vhost1 mirrorAS 4 AS cn

systemctl start apache2

curl -s 127.0.0.1 | grep downloads

mb new mirrorNA --http http://127.0.0.2 --rsync rsync://127.0.0.2/downloads --country us --region NA
mb scan --enable mirrorNA
mb new mirrorEU --http http://127.0.0.3 --rsync rsync://127.0.0.3/downloads --country de --region EU
mb scan --enable mirrorEU
mb new mirrorAS --http http://127.0.0.4 --rsync rsync://127.0.0.4/downloads --country cn --region AS
mb scan --enable mirrorAS
