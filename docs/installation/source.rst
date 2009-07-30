Installing from source
======================


Install Apache 2.2.6 or later. 


The main Apache module can be built with the following steps::

  # unpack the tarball
  cd mod_mirrorbrain
  apxs2 -c mod_mirrorbrain.c

To install the module in the right place, you would typically call::

  apxs2 -i mod_mirrorbrain.c

Building, installing and activation can typically be combined in one apxs call::

  apxs2 -cia mod_mirrorbrain.c


After installation of mod_mirrorbrain, you'll need to:


* install GeoIP library, commandline tools, and geoip apache module
  (openSUSE/SLE packages: GeoIP, libGeoIP1, apache2-mod_geoip)

* configure mod_geoip::

    GeoIPEnable On
    GeoIPOutput Env
    GeoIPDBFile /var/lib/GeoIP/GeoIP.dat MMapCache

  (You would typically put this into the server-wide context of a virtual host.)

  Note that a caching mode like MMapCache needs to be used, when Apache runs with the worker MPM.
  See http://forum.maxmind.com/viewtopic.php?p=2078 for more information.

* install mod_form. The sources are here:

  * http://apache.webthing.com/svn/apache/forms/mod_form.c
  * http://apache.webthing.com/svn/apache/forms/mod_form.h

- install the following Python modules:

  * python-cmdln
  * python-sqlobject
  * python-psycopg2

- for the scanner, which is written in Perl, a few Perl modules are required:

  * perl-Config-IniFiles
  * perl-libwww-perl
  * perl-DBD-Pg
  * perl-TimeDate
  * perl-Digest-MD4 (it is not *really* needed, but prevents an ugly error message)

- configure the database to use with MirrorBrain, and continue with the respective
  description below:


  * install the PostgreSQL database adapter for the DBD library
    Note that if the web server is set up seperately from the database server,
    only the web server needs this package.
  
  * install postgresql and start it

  * create the postgresql user account and database::


       su - postgres
       
       root@powerpc:~ # su - postgres
       postgres@powerpc:~> createuser -P mb
       Enter password for new role: 
       Enter it again: 
       Shall the new role be a superuser? (y/n) n
       Shall the new role be allowed to create databases? (y/n) n
       Shall the new role be allowed to create more new roles? (y/n) n
       CREATE ROLE
       
       postgres@powerpc:~> createdb -O mb mb_samba
       CREATE DATABASE
       postgres@powerpc:~> createlang plpgsql mb_samba
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
       host    mb_samba    mb          10.10.2.3/32          md5

       
      
  *  Tuning:

     If the database will be large, reserve enough memory for it (mainly
     by setting shared_buffers), and in any case you should switch off
     synchronous commit mode (synchronous_commit = off). This can be set in
     data/postgresql.conf.

     Start the server::

       root@powerpc:~ # rcpostgresql restart

  * import table structure, and initial data::

       psql -U mb -f sql/schema-postgresql.sql mb_samba
       psql -U mb -f sql/initialdata-postgresql.sql mb_samba


* Create a user and group::

    groupadd -r mirrorbrain
    useradd -r -o -g mirrorbrain -s /bin/bash -c "MirrorBrain user" -d /home/mirrorbrain mirrorbrain

* create :file:`/etc/mirrorbrain.conf` with the content below. File permissions
  should be 0640, ownership root:mirrorbrain::

    [general]
    instances = samba
  
    [samba]
    dbuser = mb
    dbpass = 12345
    dbdriver = postgresql
    dbhost = your_host.example.com
    # optional: dbport = ...
    dbname = mb_samba
    
    [mirrorprobe]
    mailto = your_mail@example.com, another_mail@example.com


* Note: the "mb" tool referenced below is (for convenience) a symlink to the
  mirrordoctor.py script.

* now you should be able to type 'mb list' without getting an error.
  It'll produce no output, but exit with 0. If it gives an error, something is
  wrong.

* collect a list of mirrors (their HTTP baseurl, and their rsync or FTP baseurl
  for scanning). For example::

    http://ftp.isr.ist.utl.pt/pub/MIRRORS/ftp.suse.com/projects/
    rsync://ftp.isr.ist.utl.pt/suse/projects/

    http://ftp.kddilabs.jp/Linux/distributions/ftp.suse.com/projects/
    rsync://ftp.kddilabs.jp/suse/projects/



  Now you need to enter the mirrors into the database; it could be done using the
  "mb" mirrorbrain tool. (See 'mb help new' for full option list.)::

    mb new ftp.isr.ist.utl.pt \
           --http http://ftp.isr.ist.utl.pt/pub/MIRRORS/ftp.suse.com/projects/ \
           --rsync rsync://ftp.isr.ist.utl.pt/suse/projects/

    mb new ftp.kddilabs.jp \
           --http http://ftp.kddilabs.jp/Linux/distributions/ftp.suse.com/projects/ \
           --rsync rsync://ftp.kddilabs.jp/suse/projects/


  The tool automatically figures out the GeoIP location of each mirror by itself.
  But you could also specify them on the commandline.

  If you want to edit a mirror later, use::

    mb edit <identifier>

  To simply display a mirror, you could use 'mb show kddi', for instance.

  Finally, each mirror needs to be scanned and enabled::

    mb scan --enable <identifier>

  See the output of 'mb help' for more commands.



* configure Apache:

  * load the Apache modules::

     a2enmod form
     a2enmod geoip
     a2enmod dbd
     a2enmod mirrorbrain

  * create a DNS alias for your web host, if needed

  * configure the database adapter (mod_dbd), resp. its connection pool.
    Put the configuration into server-wide context. Config example::

      # for prefork, this configuration is inactive. prefork simply uses 1
      # connection per child.
      <IfModule !prefork.c>
              DBDMin  0
              DBDMax  32
              DBDKeep 4
              DBDExptime 10
      </IfModule>

  * configure the database driver.
    Put the following configuration into server-wide OR vhost context. Make the file
    chmod 0640, owned root:root because it will contain the database password::

      DBDriver pgsql
      # note that the connection string (which is passed straight through to
      # PGconnectdb in this case) looks slightly different - pass vs. password
      DBDParams "host=localhost user=mb password=12345 dbname=mb_samba connect_timeout=15"


    .. note:: The database connection string must be unique per virtual host.
              This matters if several MirrorBrain instances are set up in one
              Apache. If the database connection string is identical in
              different virtual hosts, mod_dbd may fail to associate the
              connection string with the correct virtual host.


  * configure mod_mirrorbrain.
    You probably want to reate a vhost (e.g.
    /etc/apache2/vhosts.d/samba.mirrorbrain.org.conf) and add the MirrorBrain
    configuration like shown here::

      <VirtualHost your.host.name:80>
          ServerName samba.mirrorbrain.org
      
          ServerAdmin webmaster@mirrorbrain.org
      
          DocumentRoot /srv/samba/pub/projects
      
          ErrorLog     /var/log/apache/samba.mirrorbrain.org/logs/error_log
          CustomLog    /var/log/apache/samba.mirrorbrain.org/logs/access_log combined

          <Directory /srv/samba/pub/projects>
              MirrorBrainEngine On
              MirrorBrainDebug Off
              FormGET On
              MirrorBrainHandleHEADRequestLocally Off
              MirrorBrainMinSize 2048
              MirrorBrainHandleDirectoryIndexLocally On
              MirrorBrainExcludeUserAgent rpm/4.4.2*
              MirrorBrainExcludeUserAgent *APT-HTTP*
              MirrorBrainExcludeMimeType application/pgp-keys

              Options FollowSymLinks Indexes
              AllowOverride None
              Order allow,deny
              Allow from all
          </Directory>
      
      </VirtualHost>

  * restart Apache, best while watching the error log::

      tail -F /var/log/apache/*_log &
      apachectl restart

    
  * mirror surveillance needs to be configured. Put this into /etc/crontab::

      -* * * * *                mirrorbrain   mirrorprobe -t 20 &>/dev/null

    Likewise, configure scanning::

      44 0,4,8,12,16,20 * * *   mirrorbrain   mb scan -j 3 -a


TODO: describe how to test that the install was successful
    (When testing, consider any excludes that you configured, and which may
    confuse you.)


TODO: describe decent logging setup


* further things that you might want to configure:

  * mod_autoindex_mb, a replacement for the standard module mod_autoindex::

      a2dismod autoindex
      a2enmod autoindex_mb
      Add IndexOptions Metalink Mirrorlist
      # or IndexOptions +Metalink +Mirrorlist, depending on your config

  * add a link to a CSS stylesheet for mirror lists::

      MirrorBrainMirrorlistStylesheet "http://static.opensuse.org/css/mirrorbrain.css"

    and for the autoindex::

      IndexStyleSheet "http://static.opensuse.org/css/mirrorbrain.css"

  * prepare the metalink hashes. 

    * First, add some configuration::

        MirrorBrainMetalinkPublisher "openSUSE" http://download.opensuse.org
        MirrorBrainMetalinkHashesPathPrefix /srv/metalink-hashes/ppc

    * install the "metalink" tool from http://metamirrors.nl/metalinks_project
      (openSUSE package called metalink, http://download.opensuse.org/repositories/network:/utilities/)
      and create the hashes::

        metalink-hasher update -t /srv/metalink-hashes/ppc/srv/ftp/pub/opensuse/ppc /srv/ftp/pub/opensuse/ppc

    * add the hashing command to /etc/crontab to be run every few hours. Alternatively, run
      it after changes in the file tree happen.


.. note:: That's how far the instructions go. I hope they are useful. Please
          subscribe to the mirrorbrain mailing list, see
          http://mirrorbrain.org/communication .  Questions can be answered there,
          feedback is appreciated.




