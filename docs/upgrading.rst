.. _upgrading:

Upgrading
=========

Upgrading PostgreSQL
--------------------

When upgrading PostgreSQL, it is important to look at the version number difference. 
If the third digit changes, no special procedure is needed (except when the
release notes explicitely hint about it).

When the first or second digit change, then a dump-and-reload cycle is usually
needed. 

For instance, when upgrading from 8.3.5 to 8.3.7 nothing needs to be done. When
upgrading from 8.3.7 to 8.4, you need to dump and reload.

You might want to follow the instructions that your vendor provides. If your
vendor doesn't provide an upgrade procedure, be warned that the database needs
to be dumped before upgrading PostgreSQL.

See ``pg_dumpall(1)`` for how to dump and reload the complete database.

.. warning:: 
   If you use mod_asn, there is one more caveat. If your vendor's upgrade
   procedure automatically saves the previous PostgreSQL binaries in case they
   are needed later, the procedure might not take into account that the
   :file:`ip4r.so` shared object might need to be saved as well.  Hence, you
   might be unable to start the old binaries, when the ip4r shared object has
   been upgraded already.

Hence, it is recommended that you do a complete dump of the databases before
upgrading, and load that after upgrading.


.. note:: 
   When upgrading to 8.4, ``ident sameuser`` is no longer a valid value in
   :file:`pg_hba.conf`. Replace it with ``ident``.



.. 
   Notes for upgrade walkthrough
   
   root@doozer ~ # rccron stop
   Shutting down CRON daemon                                             done
   root@doozer ~ # su - postgres
   postgres@doozer:~> pg_dumpall > SAVE
   postgres@doozer:~> 
   
   
   root@doozer ~ # rcpostgresql stop
   Shutting down PostgreSQLserver stopped                                done
   
   
     >>>> Run the update here <<<<
   
   
   root@doozer ~ # old /var/lib/pgsql/data 
   moving /var/lib/pgsql/data to /var/lib/pgsql/data-20090728
   root@doozer ~ # rcpostgresql start
   Initializing the PostgreSQL database at location /var/lib/pgsql/data  done
   Starting PostgreSQL                                                   done
   root@doozer ~ # 
   
   
   postgres@doozer:~> psql template1 -f SAVE
   [...]
   
   postgres@doozer:~> cp data/pg_hba.conf data/pg_hba.conf.orig
   postgres@doozer:~> cp data/postgresql.conf data/postgresql.conf.orig
   postgres@doozer:~> vi -d data-20090728/pg_hba.conf data/pg_hba.conf 
   postgres@doozer:~> vi -d data-20090728/postgresql.conf data/postgresql.conf
   
   
   
   
   
   root@doozer ~ # rcapache2 reload
   Reload httpd2 (graceful restart)                                      done
   root@doozer ~ # rccron start
   Starting CRON daemon                                                  done
   
   
   
   
   
   mirrordb:
   cron stop on batavia510
   cron stop on mirrordb
   repopusher stop
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   using a temporary PostgreSQL daemon:
   
 ..
    # mkdir /space/pgsql-tmp
    # chown postgres:postgres /space/pgsql-tmp
    # su - postgres   
 ..
   postgres@mirrordb:~> 
   
   
   postgres@mirrordb:~> initdb /space/pgsql-tmp/data
   The files belonging to this database system will be owned by user "postgres".
   This user must also own the server process.
   
   The database cluster will be initialized with locale en_US.UTF-8.
   The default database encoding has accordingly been set to UTF8.
   The default text search configuration will be set to "english".
   
   creating directory /space/pgsql-tmp/data ... ok
   creating subdirectories ... ok
   selecting default max_connections ... 100
   selecting default shared_buffers/max_fsm_pages ... 32MB/204800
   creating configuration files ... ok
   creating template1 database in /space/pgsql-tmp/data/base/1 ... ok
   initializing pg_authid ... ok
   initializing dependencies ... ok
   creating system views ... ok
   loading system objects' descriptions ... ok
   creating conversions ... ok
   creating dictionaries ... ok
   setting privileges on built-in objects ... ok
   creating information schema ... ok
   vacuuming database template1 ... ok
   copying template1 to template0 ... ok
   copying template1 to postgres ... ok
   
   WARNING: enabling "trust" authentication for local connections
   You can change this by editing pg_hba.conf or using the -A option the
   next time you run initdb.
   
   Success. You can now start the database server using:
   
       postgres -D /space/pgsql-tmp/data
   or
       pg_ctl -D /space/pgsql-tmp/data -l logfile start
   
   postgres@mirrordb:~> 

