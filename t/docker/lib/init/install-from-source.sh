
( 
cd /opt/project
# install -m 755 tools/geoiplookup_continent /usr/bin/geoiplookup_continent
# install -m 755 tools/geoiplookup_city      /usr/bin/geoiplookup_city
# install -m 755 tools/geoip-lite-update     /usr/bin/geoip-lite-update
# install -m 755 tools/null-rsync            /usr/bin/null-rsync
install -m 755 tools/scanner.pl            /usr/bin/scanner

cd /opt/project/mod_mirrorbrain
apxs -cia -lm mod_mirrorbrain.c
)

( 
rm /usr/bin/mb
rm -f /usr/lib64/python*/site-packages/mb/*
cd /opt/project/mb && python3 setup.py install
)

systemctl start postgresql

port=80

setup_vhost() {
    local site=$1 
    local port=$2
    cp /etc/apache2/vhosts.d/vhost.template /etc/apache2/vhosts.d/$site.conf
    sed -i "s,dummy-host.example.com,$site," /etc/apache2/vhosts.d/$site.conf
    sed -i "s,:80,:$port," /etc/apache2/vhosts.d/$site.conf
    if [ "$port" != 80 ]; then
        echo Listen $port >> /etc/apache2/listen.conf
    else
        sed -i "/ServerSignature On/a DBDriver pgsql\nDBDParams \"host=localhost user=mirrorbrain password=mirrorbrain dbname=mirrorbrain connect_timeout=15\"\nMirrorBrainMetalinkPublisher '$site' http://127.0.0.1" /etc/apache2/vhosts.d/$site.conf
        sed -i '/Directory "\/srv\/www\/vhosts\/MAIN"/a MirrorBrainEngine On\n        MirrorBrainDebug On\n        FormGET On\n        MirrorBrainHandleHEADRequestLocally Off\n        MirrorBrainMinSize 0\n        MirrorBrainExcludeUserAgent rpm\/4.4.2*\n        MirrorBrainExcludeUserAgent *APT-HTTP*\n        MirrorBrainExcludeMimeType application\/pgp-keys' /etc/apache2/vhosts.d/$site.conf
        mkdir -p /srv/hashes/srv/www/vhosts/$site/downloads/
        chown -R mirrorbrain:mirrorbrain srv/hashes/srv/www/vhosts/$site/downloads/
    fi

    mkdir -p /srv/www/vhosts/$site/downloads/{folder1,folder2,folder3}
    echo /srv/www/vhosts/$site/downloads/{folder1,folder2,folder3}/{file1,file2}.dat | xargs -n 1 touch
}

setup_vhost MAIN $((port++))

a2enmod form
a2enmod maxminddb
a2enmod dbd
# a2enmod mirrorbrain

mb makehashes /srv/www/vhosts/MAIN
