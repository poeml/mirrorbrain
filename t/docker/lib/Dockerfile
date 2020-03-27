# Defines the tag for OBS and build script builds:
FROM registry.opensuse.org/home/andriinikitin/containers/serviced 

ADD src/sql /usr/share/doc/packages/mirrorbrain/sql

RUN sudo -u postgres /usr/share/postgresql/postgresql-script start && \
    sudo -u postgres createuser mirrorbrain && \
    sudo -u postgres createdb mirrorbrain && \
    sudo -u postgres psql -c "alter user mirrorbrain with encrypted password 'mirrorbrain';" && \
    sudo -u postgres psql -c "CREATE EXTENSION ip4r;" mirrorbrain && \
    sudo -u mirrorbrain psql -f /usr/share/doc/packages/mirrorbrain/sql/schema-postgresql.sql mirrorbrain && \
    sudo -u mirrorbrain psql -f /usr/share/doc/packages/mirrorbrain/sql/migrations/schema-postgresql-add-filearr_split_path_trigger.sql mirrorbrain && \
    sudo -u mirrorbrain psql -f /usr/share/doc/packages/mirrorbrain/sql/initialdata-postgresql.sql mirrorbrain && \
    sudo -u postgres /usr/share/postgresql/postgresql-script stop

RUN sed -i 's,127.0.0.1/32            ident,127.0.0.1/32            password,' /var/lib/pgsql/data/pg_hba.conf
RUN sed -i 's,\#log_min_duration_statement = -1,log_min_duration_statement = 500,' /var/lib/pgsql/data/postgresql.conf
RUN sed -i 's,\#log_lock_waits = off,log_lock_waits = on,' /var/lib/pgsql/data/postgresql.conf
RUN sed -i 's,\#deadlock_timeout = 1s,deadlock_timeout = 10,' /var/lib/pgsql/data/postgresql.conf

ADD mirrorbrain.conf /etc/mirrorbrain.conf
RUN chmod 0640 /etc/mirrorbrain.conf
RUN chown root:mirrorbrain /etc/mirrorbrain.conf

ENTRYPOINT  ["/usr/lib/systemd/systemd"]
