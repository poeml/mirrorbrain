
Installation on Debian/Ubuntu Linux
===================================

.. note:: 
   The following recipe for installing MirrorBrain was tested on Ubuntu 9.04.
   A similar procedure should work on Debian 5.0 as well.


Install a standard Ubuntu LAMP server.


Add package repository
----------------------

To subscribe to the repository with packages for Ubuntu 9.04, add the following
to :file:`/etc/apt/sources.list`::

   sudo vim /etc/apt/sources.list
  [...]
  deb http://download.opensuse.org/repositories/Apache:/MirrorBrain/xUbuntu_9.04/ /


There are more repositories at
http://download.opensuse.org/repositories/Apache:/MirrorBrain/ for other Ubuntu
and Debian releases.


After adding the repository, update the local :program:`apt-get` package
cache::

  sudo apt-get update


Install the MirrorBrain packages
--------------------------------

The following commands will install all needed software via
:program:`apt-get`::

  sudo apt-get install mirrorbrain mirrorbrain-tools mirrorbrain-scanner \
  libapache2-mod-mirrorbrain libapache2-mod-autoindex-mb


.. note:: 
   The packages are unsigned, thus a corresponding warning needs to be
   answered with 'y'.


Install an Apache MPM
---------------------

The MirrorBrain packages have dependencies on the Apache common packages, but
not on a MPM, since the choice of an MPM is one that the system admin must
make, and the MPMs cannot be installed in parallel. Thus, an MPM needs to be
installed (unless a LAMP package selection was installed initially). 

To install the worker MPM, run::

  sudo apt-get install apache2-mpm-worker

*If* the LAMP server has been installed, the prefork MPM was probably
preselected. It may make sense to switch to the worker MPM in such cases, which
is a good choice for busy download servers.


Configure mod_geoip
~~~~~~~~~~~~~~~~~~~

mod_geoip must be configured to find the the GeoIP data set::

  sudo sh -c "cat > /etc/apache2/mods-available/geoip.conf << EOF
  <IfModule mod_geoip.c>
   GeoIPEnable On
   GeoIPOutput Env
   GeoIPDBFile /var/lib/GeoIP/GeoIP.dat Standard
  </IfModule>
  EOF
  " 

Download GeoIP data set::

  wget http://geolite.maxmind.com/download/geoip/database/GeoLiteCountry/GeoIP.dat.gz
  gunzip GeoIP.dat.gz
  sudo mkdir /var/lib/GeoIP
  sudo cp GeoIP.dat /var/lib/GeoIP/GeoIP.dat

Enable module and restart Apache::

  sudo a2enmod geoip
  sudo /etc/init.d/apache2 restart


Configure mod_form
~~~~~~~~~~~~~~~~~~

Enable module and restart Apache::

  sudo a2enmod form
  sudo /etc/init.d/apache2 restart


Configure mod_dbd
~~~~~~~~~~~~~~~~~

With Ubuntu 9.04, the DBD (Apache Portable Runtime DBD Framework) database
adapter for PostgreSQL is already installed, because the driver is statically
linked into the libaprutil1 shared object. libaprutil1-dbd-pgsql is a virtual
package which is just a pointer to the libaprutil1 package.

Running the following snippet will create a configuration for mod_dbd::

  sudo sh -c "cat > /etc/apache2/mods-available/dbd.conf << EOF
   <IfModule mod_dbd.c>
      DBDriver pgsql
      DBDParams 'host=localhost user=mirrorbrain password=12345 dbname=mirrorbrain connect_timeout=15'
   </IfModule>
  EOF
  "


Enable module and restart Apache::

  sudo a2enmod dbd
  sudo /etc/init.d/apache2 restart


Configure mod_mirrorbrain
~~~~~~~~~~~~~~~~~~~~~~~~~

Enable module and restart Apache::

  sudo a2enmod mirrorbrain
  sudo /etc/init.d/apache2 restart


Install PostgreSQL
------------------

Install the PostgreSQL server (here, version 8.3 is the current version)::

  sudo apt-get install postgresql-8.3


Create the postgresql user account and database
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Switch to user postgres::

  sudo su - postgres

Create user::

  createuser -P mirrorbrain
  Enter password for new role: 
  Enter it again: 
  Shall the new role be a superuser? (y/n) n
  Shall the new role be allowed to create databases? (y/n) n
  Shall the new role be allowed to create more new roles? (y/n) n

Create database::

  createdb -O mirrorbrain mirrorbrain
  createlang plpgsql mirrorbrain

Exit user postgres::

  exit


Edit host-based authentication 
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Add line ``local mirrorbrain mirrorbrain 127.0.0.1/32 md5`` to the end of
:file:`pg_hba.conf`, which is to be found here::

  sudo vim /etc/postgresql/8.3/main/pg_hba.conf

Start the PostgreSQL server::

  sudo /etc/init.d/postgresql-8.3 restart

Create needed users and groups
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Create user and group ``mirrorbrain``::

  sudo groupadd -r mirrorbrain
  sudo useradd -r -g mirrorbrain -s /bin/bash -c "MirrorBrain user" -d /home/mirrorbrain mirrorbrain

Import initial mirrorbrain data
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Import the table structure, and initial data::

  wget http://mirrorbrain.org/files/releases/mirrorbrain-2.10.1.tar.gz
  tar -zxvf mirrorbrain-2.10.1.tar.gz
  cd mirrorbrain-2.10.1
  sudo - mirrorbrain
  psql -U mirrorbrain -f sql/schema-postgresql.sql mirrorbrain
  psql -U mirrorbrain -f sql/initialdata-postgresql.sql mirrorbrain
  exit

Create needed directories
~~~~~~~~~~~~~~~~~~~~~~~~~

Create the following directory for logs, and give ownership to the new
mirrorbrain user::

  sudo mkdir /var/log/mirrorbrain
  sudo chown mirrorbrain:mirrorbrain /var/log/mirrorbrain
  sudo chmod 0750 /var/log/mirrorbrain


Create mirrorbrain.conf
~~~~~~~~~~~~~~~~~~~~~~~

Create a configuration file named :file:`mirrorbrain.conf`::

  sudo sh -c "cat > /etc/mirrorbrain.conf << EOF
  [general]
  instances = main
  
  [main]
  dbuser = mirrorbrain
  dbpass = 12345
  dbdriver = postgresql
  dbhost = 127.0.0.1
  # optional: dbport = ...
  dbname = mirrorbrain
  
  [mirrorprobe]
  # logfile = /var/log/mirrorbrain/mirrorprobe.log
  # loglevel = INFO

  EOF
  "

Set permission and privileges on the file::

  sudo chmod 0604 /etc/mirrorbrain.conf 
  sudo chown root:mirrorbrain /etc/mirrorbrain.conf


Test mirrorbrain
~~~~~~~~~~~~~~~~

If the following command returns no error, but rather displays its usage info,
the installation should be quite fine::

  mb help


Create a virtual host
---------------------

The following snippet would create a new site as virtual host::

  sudo sh -c "cat > /etc/apache2/sites-available/mirrorbrain << EOF
   <VirtualHost 127.0.0.1>
     ServerName mirrors.example.org
     ServerAdmin webmaster@example.org
     DocumentRoot /var/www/downloads
     ErrorLog     /var/log/apache2/mirrors.example.org/error_log
     CustomLog    /var/log/apache2/mirrors.example.org/access_log combined
     <Directory /var/www/downloads>
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
  EOF
  "

Make the log directory::

  sudo mkdir /var/log/apache2/mirrors.example.org/

Make the download directory::

  sudo mkdir /var/www/downloads

Enable the site::

  sudo a2ensite mirrorbrain


Restart Apache::

  sudo /etc/init.d/apache2 restart


