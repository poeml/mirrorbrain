
Installing from source
======================


Installing Apache
-----------------

Build or install Apache 2.2.6 or later. 

When installing/building the Apache Portable Runtime (APR), make sure that you
build the required database adapter for the DBD module for PostgreSQL.


Building Apache modules
-----------------------

mod_form
~~~~~~~~

Install mod_form. The sources are here:

* http://apache.webthing.com/svn/apache/forms/mod_form.c
* http://apache.webthing.com/svn/apache/forms/mod_form.h

Its header file will be needed later to compile mod_mirrorbrain.

It is useful to apply the following patch to mod_form.c::

  Tue Mar 13 15:16:30 CET 2007 - poeml@cmdline.net
  
  preserve r->args (apr_strtok is destructive in this regard). Makes
  mod_autoindex work again in conjunction with directories where FormGET is
  enabled.
  
  --- mod_form.c.old      2007-03-13 15:05:13.872945000 +0100
  +++ mod_form.c  2007-03-13 15:06:26.378367000 +0100
  @@ -61,6 +61,7 @@
     char* pair ;
     char* last = NULL ;
     char* eq ;
  +  char* a ;
     if ( ! ctx ) {
       ctx = apr_pcalloc(r->pool, sizeof(form_ctx)) ;
       ctx->delim = delim[0];
  @@ -69,7 +70,8 @@
     if ( ! ctx->vars ) {
       ctx->vars = apr_table_make(r->pool, 10) ;
     }
  -  for ( pair = apr_strtok(args, delim, &last) ; pair ;
  +  a = apr_pstrdup(r->pool, args);
  +  for ( pair = apr_strtok(a, delim, &last) ; pair ;
           pair = apr_strtok(NULL, delim, &last) ) {
       for (eq = pair ; *eq ; ++eq)
         if ( *eq == '+' )


mod_mirrorbrain
~~~~~~~~~~~~~~~

The main Apache module, :program:`mod_mirrorbrain`, can be built with the
following steps::

  # unpack the tarball and go inside the source tree
  cd mod_mirrorbrain
  apxs -c mod_mirrorbrain.c

To install the module in the right place, you would normally call::

  apxs -i mod_mirrorbrain.c

Building, installing and activation can typically be combined in one :program:`apxs` call::

  apxs -cia mod_mirrorbrain.c


mod_autoindex_mb
~~~~~~~~~~~~~~~~

Build and install mod_autoindex_mb::

  # in mirrorbrain source tree
  cd mod_autoindex_mb
  apxs -cia mod_autoindex_mb.c


mod_geoip
~~~~~~~~~

Install the GeoIP library, the accompanying commandline tools, and the geoip Apache module.

Configure mod_geoip::

  GeoIPEnable On
  GeoIPOutput Env
  GeoIPDBFile /var/lib/GeoIP/GeoIP.dat MMapCache

(You would typically put this into the server-wide context of a virtual host.)

Note that a caching mode like MMapCache needs to be used, when Apache runs with the worker MPM.
See http://forum.maxmind.com/viewtopic.php?p=2078 for more information.

You need to build two commandline tools for GeoIP::

  cd tools
  gcc -Wall -o geoiplookup_continent geoiplookup_continent.c -lGeoIP
  gcc -Wall -o geoiplookup_city geoiplookup_city.c -lGeoIP



Installing Python and Perl modules
----------------------------------

Install the following Python modules:

* python-cmdln
* python-sqlobject
* python-psycopg2

Install a few Perl modules as well (required for the mirror scanner, which is written in Perl):

* perl-Config-IniFiles
* perl-libwww-perl
* perl-DBD-Pg
* perl-TimeDate
* perl-Digest-MD4 (it is not *really* needed, but prevents an ugly error message)


Installing PostgreSQL
---------------------

Install the PostgreSQL server, start it and create a user and a database::

  su - postgres
  
  root@powerpc:~ # su - postgres
  postgres@powerpc:~> createuser -P mirrorbrain
  Enter password for new role: 
  Enter it again: 
  Shall the new role be a superuser? (y/n) n
  Shall the new role be allowed to create databases? (y/n) n
  Shall the new role be allowed to create more new roles? (y/n) n
  CREATE ROLE
  
  postgres@powerpc:~> createdb -O mirrorbrain mirrorbrain
  CREATE DATABASE
  postgres@powerpc:~> createlang plpgsql mirrorbrain
  postgres@powerpc:~> 


  postgres@powerpc:~> cp data/pg_hba.conf data/pg_hba.conf.orig
  postgres@powerpc:~> vi data/pg_hba.conf

  # TYPE  DATABASE    USER        CIDR-ADDRESS          METHOD
  # "local" is for Unix domain socket connections only
  #local   all         all                               ident
  local   all         all                               password
  # IPv4 local connections:
  host    all         all         127.0.0.1/32          password
  # IPv6 local connections:
  host    all         all         ::1/128               password
  # remote connections:
  host    mirrorbrain mirrorbrain 10.10.2.3/32          md5


Install the ip4r data type.

Import the table structure and initial data::

  psql -U mirrorbrain -f sql/schema-postgresql.sql mirrorbrain
  psql -U mirrorbrain -f sql/initialdata-postgresql.sql mirrorbrain



Creating a "mirrorbrain" user and group
---------------------------------------

Create a "mirrorbrain" user and group::

  groupadd -r mirrorbrain
  useradd -r -o -g mirrorbrain -s /bin/bash -c "MirrorBrain user" -d /home/mirrorbrain mirrorbrain


Installation of the tools
-------------------------

You need to install a number of the provided tools to a location in your $PATH.
Unfortunately, there is no Makefile to take this work off you. Hopefully, one can
be provided later::

  install -m 755 tools/geoiplookup_continent /usr/bin/geoiplookup_continent
  install -m 755 tools/geoiplookup_city      /usr/bin/geoiplookup_city
  install -m 755 tools/geoip-lite-update     /usr/bin/geoip-lite-update
  install -m 755 tools/null-rsync            /usr/bin/null-rsync
  install -m 755 tools/scanner.pl            /usr/bin/scanner
  install -m 755 mirrorprobe/mirrorprobe.py  /usr/bin/mirrorprobe


The following command should build and install the :program:`mb` admin tool::

  setup.py install [--prefix=...]
  ln -s mb.py /usr/bin/mb



Configuring Apache
------------------

Load the Apache modules::

  a2enmod form
  a2enmod geoip
  a2enmod dbd
  a2enmod mirrorbrain


Configure the database adapter (mod_dbd), resp. its connection pool.
Put the configuration into server-wide context. Config example::

  # for prefork, this configuration is inactive. prefork simply uses 1
  # connection per child.
  <IfModule !prefork.c>
          DBDMin  0
          DBDMax  32
          DBDKeep 4
          DBDExptime 10
  </IfModule>

Configure the database driver. Put the following configuration into server-wide
OR vhost context. Make the file chmod 0640, owned root:root because it will
contain the database password::

  DBDriver pgsql
  # note that the connection string (which is passed straight through to
  # PGconnectdb in this case) looks slightly different - pass vs. password
  DBDParams "host=localhost user=mirrorbrain password=12345 dbname=mirrorbrain connect_timeout=15"


.. note:: The database connection string must be unique per virtual host.
          This matters if several MirrorBrain instances are set up in one
          Apache. If the database connection string is identical in
          different virtual hosts, mod_dbd may fail to associate the
          connection string with the correct virtual host.



Next steps
----------

From here, follow on with :ref:`initial_configuration`.
