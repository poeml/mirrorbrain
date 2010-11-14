.. _upgrading:

Upgrading
=========

.. _refreshing_package_sign_keys:

Refreshing package sign keys
----------------------------

The key that is used to sign packages may expire from time to time, and needs
to be renewed. This is done by the build maintainers, and you may need update
your installation to know about the refreshed key.

If you see the following (on Debian or Ubuntu), this affects you::

    lenny:~# apt-get update
    [...]
    Reading package lists... Done
    W: GPG error: http://download.opensuse.org  Release: The following signatures were invalid: KEYEXPIRED 1270154736
    W: You may want to run apt-get update to correct these problems
    lenny:~#

After running the following command, all should work fine again::

    lenny:~# apt-key adv --keyserver hkp://wwwkeys.de.pgp.net --recv-keys BD6D129A

If it does not help, it is probably best to contact the build maintainers or
the mirrorbrain mailing list.


Upgrading PostgreSQL
--------------------

General notes
^^^^^^^^^^^^^

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



Offline upgrade
^^^^^^^^^^^^^^^

The following console transcripts should give an idea about upgrading an
PostgreSQL installation. It was done on an openSUSE system, but a similar
procedure should work on other platforms.

The upgrade in this example is done while the database is taken offline, i.e.
you need to plan for a downtime of the server. The cron daemon is stopped so
that there are no attempted writes to the database. :program:`pg_dumpall` is
used to save the complete database to a file::

   root@doozer ~ # rccron stop
   Shutting down CRON daemon                                             done
   root@doozer ~ # su - postgres
   postgres@doozer:~> pg_dumpall > SAVE
   postgres@doozer:~> exit
   root@doozer ~ # rcpostgresql stop
   Shutting down PostgreSQL server stopped                               done
   
   
At this point, you would upgrade the PostgreSQL software.

Next, the :file:`data` directory is moved away, a new one created::

   root@doozer ~ # old /var/lib/pgsql/data 
   moving /var/lib/pgsql/data to /var/lib/pgsql/data-20090728
   root@doozer ~ # rcpostgresql start
   Initializing the PostgreSQL database at location /var/lib/pgsql/data  done
   Starting PostgreSQL                                                   done
   root@doozer ~ # 

Now, the authentication setup and the configuration need to be migrated from
the old install to the new one::

   root@doozer ~ # su - postgres
   postgres@doozer:~> cp data/pg_hba.conf data/pg_hba.conf.orig
   postgres@doozer:~> cp data/postgresql.conf data/postgresql.conf.orig
   postgres@doozer:~> vi -d data-20090728/pg_hba.conf data/pg_hba.conf 
   postgres@doozer:~> vi -d data-20090728/postgresql.conf data/postgresql.conf
   
Then, load the dump into the new database::

   postgres@doozer:~> psql template1 -f SAVE
   [...]
   
Finally, restart PostgreSQL, Apache and cron::   
   
   root@doozer ~ # rcpostgresql restart
   Shutting down PostgreSQL server stopped                               done
   Starting PostgreSQL                                                   done
   root@doozer ~ # rcapache2 reload
   Reload httpd2 (graceful restart)                                      done
   root@doozer ~ # rccron start
   Starting CRON daemon                                                  done
   
   
   
Online upgrade
^^^^^^^^^^^^^^

Using a second PostgreSQL daemon, started temporarily, an online
upgrade can be performed as follows.

First, create space for the temporary database::
   
   root@mirrordb ~ # mkdir /space/pgsql-tmp
   root@mirrordb ~ # chown postgres:postgres /space/pgsql-tmp

Create the new (temporary) database::

   root@mirrordb ~ # su - postgres   
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


Copy the configuration and the authentification setup to the temporary database::

   postgres@mirrordb:~> cp /space/pgsql/data/postgresql.conf /space/pgsql-tmp/data/
   postgres@mirrordb:~> cp /space/pgsql/data/pg_hba.conf /space/pgsql-tmp/data/

.. note::
   The second database server will need RAM — maybe it will be necessary to
   adjust the ``shared_buffers`` setting in :file:`postgresql.conf` for both
   daemons, so they don't try allocate more memory than physically available.

Next, change the port in the temporary :file:`postgresql.conf` from 5432 to
5433 and start the second PostgreSQL server::

   postgres@mirrordb:~> vi /space/pgsql-tmp/data/postgresql.conf
   postgres@mirrordb:~> postgres -D /space/pgsql-tmp/data

.. note::
   This assumes that Apache is configured to use a TCP connection to access the
   database server, not a UNIX domain socket.

Load the dumped data (not forgetting to use the differing port number)::

   postgres@doozer:~> psql -p 5433 template1 -f SAVE
   [...]

Now the Apache server, and possibly other services
(:file:`/etc/mirrorbrain.conf`) need to be changed to the temporary port, and
(gracefully) restarted.

.. note::
   Verify that everything works as expected with the temporary database. If it
   does, stop the primary PostgreSQL server (and verify again that everything
   still works).

From here on, the next steps are probably obvious. You would proceed as
described in the previous section. After upgrading the PostgreSQL install,
loading the data, copying/merging :file:`postgresql.conf` and
:file:`pg_hba.conf`, you would revert the Apache configuration to use port 5432
and reload it.

If everything works, you can stop and remove the temporary database installation.


..   
   additional steps on mirrordb:
   cron stop on batavia510
   cron stop on mirrordb
   repopusher stop
   


Version-specific upgrade notes
-------------------------------

To `2.14.0 <http://mirrorbrain.org/docs/changes/#release-2-14-0-r8210-nov-6-2010>`_:
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

To take advantage of mirror selection by geographical distance (as additional criterion to country, network prefix etc.), the free `GeoLite City
<http://www.maxmind.com/app/geolitecity>`_ GeoIP database needs to be used. If
you used the simpler database so far, you need to switch to the city edition
which contains the required data. The following steps are necessary:

1) Use the provided :program:`geoip-lite-update` tool to
   download it (and keep it updated regularly via cron). 
   
2) Edit your mod_geoip configuration and change ``GeoIP.dat`` to ``GeoLiteCity.dat.updated``

3) Run :program:`mb update -A --all-mirrors` to update the mirrors' GeoIP data (coordinates, country and region), and (if :program:`mod_asn` is used) autonomous system and prefix.

MirrorBrain will continue to run fine without the extra data. Thus, it is not
obligatory to switch to the larger GeoIP database.



From 2.13.x to `2.13.3 <http://mirrorbrain.org/docs/changes/#release-2-13-3-r8166-sep-26-2010>`_:
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^


If you actively use Torrents, it may make sense to recreate the hashes. It is
not strictly necessary, because the only hash that has been changed is the
BitTorrent info hash (``btih``) that is cached in the database. That hash is
used only if requested by ``<URL>.btih`` and it is shown on the details page,
but it is not used in actual Torrent generation. Thus it is unlikely to matter.
Anyhow, it could cause confusion.

You can use :program:`mb makehashes` with the ``--force`` option once to
recreate the hashes.



From 2.12.x to `2.13.0 <http://mirrorbrain.org/docs/changes/#release-2-13-0-r8123-sep-6-2010>`_:
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

* If you created hashes in the past, please edit your :file:`/etc/crontab` and
  replace the calls to the former tool ``metalink-hasher update`` with a call
  to ``mb makehashes``. Example::

    # old tool:  metalink-hasher update -t /srv/metalink-hashes/srv/ooo /srv/ooo
    # new tool:  mb makehashes -t /srv/metalink-hashes/srv/ooo /srv/ooo

* If you used the the :program:`metalink-hasher` with the ``-b`` option in the
  past, check the usage examples that come with :program:`mb help makehashes`.
  The option has been rewritten to be easier to use, and it should now be
  easier to get it to do what you want.



From 2.11.1 to `2.11.2 <http://mirrorbrain.org/docs/changes/#release-2-11-2-r7917-dec-5-2009>`_:
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The :program:`mb vacuum` command has been renamed to :program:`mb db vacuum`.
The old command will continue to work for now - but existing cron jobs should
be updated; the old command might be depracated later.

Users that happen to use the :program:`mirrorprobe` with the default timeout of
60 seconds should now run it with ``-t 60``, because the default has been
lowered to 20 seconds with release.



From 2.10.3 to `2.11.0 <http://mirrorbrain.org/docs/changes/#release-2-11-0-r7896-dec-2-2009>`_:
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``MirrorBrainHandleDirectoryIndexLocally`` directive has been removed.  A
warning is issued where it is still found in the config. It didn't really have
a function.

