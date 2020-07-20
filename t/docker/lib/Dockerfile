###### FROM registry.opensuse.org/home/andriinikitin/containers/serviced

#!BuildTag: serviced
FROM opensuse/leap:15.1
ENV container docker

ENV LANG en_US.UTF-8

RUN zypper -n addrepo https://download.opensuse.org/repositories/openSUSE:/infrastructure/openSUSE_Leap_15.1/ openSUSE:infrastructure
RUN zypper mr -p 97 openSUSE:infrastructure
RUN zypper -n addrepo https://download.opensuse.org/repositories/server:/database:/postgresql/openSUSE_Leap_15.1/ server:database:postgresql
RUN zypper mr -p 98 server:database:postgresql

RUN zypper -n --gpg-auto-import-keys --no-gpg-checks refresh

RUN zypper -n install systemd curl hostname iputils vim command-not-found bsdtar zip sudo wget gcc gzip patch

RUN systemctl mask dev-mqueue.mount dev-hugepages.mount \
    systemd-remount-fs.service sys-kernel-config.mount \
    sys-kernel-debug.mount sys-fs-fuse-connections.mount \
    display-manager.service graphical.target systemd-logind.service

ADD dbus.service /etc/systemd/system/dbus.service
RUN zypper -n install postgresql12 postgresql12-server postgresql12-ip4r \
    apache2-devel apache2-worker apache2-mod_asn apache2-mod_maxminddb apache2-mod_form \
    mirrorbrain mirrorbrain-scanner mirrorbrain-tools \
    python3-devel python3-cmdln python3-SQLObject python3-psycopg2 python3-FormEncode \
    'libmaxminddb-devel>=1.4.2' 'apache2-mod_maxminddb>=1.2.0' 'python3-geoip2>=3.0.0' 'python3-maxminddb>=1.5.2' 'meson>=0.54'


VOLUME ["/sys/fs/cgroup"]
VOLUME ["/run"]
VOLUME ["/opt/project"]

RUN systemctl enable dbus.service
RUN systemctl enable postgresql

######

ADD src/sql /usr/share/doc/packages/mirrorbrain/sql

RUN sudo -u postgres /usr/share/postgresql/postgresql-script start && \
    sudo -u postgres createuser mirrorbrain && \
    sudo -u postgres createdb mirrorbrain && \
    sudo -u postgres psql -c "alter user mirrorbrain with encrypted password 'mirrorbrain';" && \
    sudo -u postgres psql -c "CREATE EXTENSION ip4r;" mirrorbrain && \
    sudo -u mirrorbrain psql -f /usr/share/doc/packages/mirrorbrain/sql/schema-postgresql.sql mirrorbrain && \
    sudo -u mirrorbrain psql -f /usr/share/doc/packages/mirrorbrain/sql/initialdata-postgresql.sql mirrorbrain && \
    sudo -u mirrorbrain psql -f /usr/share/doc/packages/mirrorbrain/sql/migrations/schema-postgresql-move-to-mirrors.sql mirrorbrain && \
    sudo -u mirrorbrain psql -f /usr/share/doc/packages/mirrorbrain/sql/migrations/schema-postgresql-migrate-data-to-mirrors.sql mirrorbrain && \
    sudo -u postgres /usr/share/postgresql/postgresql-script stop

RUN sed -i 's,127.0.0.1/32            ident,127.0.0.1/32            password,' /var/lib/pgsql/data/pg_hba.conf
RUN sed -i 's,\#log_min_duration_statement = -1,log_min_duration_statement = 500,' /var/lib/pgsql/data/postgresql.conf
RUN sed -i 's,\#log_lock_waits = off,log_lock_waits = on,' /var/lib/pgsql/data/postgresql.conf
RUN sed -i 's,\#deadlock_timeout = 1s,deadlock_timeout = 10,' /var/lib/pgsql/data/postgresql.conf

ADD mirrorbrain.conf /etc/mirrorbrain.conf
ADD mirrorbrain-ci-city.mmdb /var/lib/GeoIP/GeoLite2-City.mmdb
ADD mirrorbrain-ci-asn.mmdb  /var/lib/GeoIP/GeoLite2-ASN.mmdb
ADD mod_maxminddb.conf /etc/apache2/conf.d/mod_maxminddb.conf
RUN chmod 0640 /etc/mirrorbrain.conf
RUN chown root:mirrorbrain /etc/mirrorbrain.conf

ENTRYPOINT  ["/usr/lib/systemd/systemd"]
