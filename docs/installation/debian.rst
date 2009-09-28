Installation on Debian/Ubuntu Linux
===================================

.. note:: 
   The following recipe for installing MirrorBrain is based on Ubuntu 9.04.


Install a standard Ubuntu LAMP server.

Download the latest MirrorBrain tarball from
http://mirrorbrain.org/files/releases/ and extract it::

  wget http://mirrorbrain.org/files/releases/mirrorbrain-$VERSION.tar.gz
  tar xzf mirrorbrain-$VERSION.tar.gz


Install Python dependencies
---------------------------

Install the following Python modules via :program:`apt-get`::

  sudo apt-get install python-sqlobject python-psycopg2

The Python :mod:`cmdln` module is not prepackaged for Ubuntu so it must be installed manually::

  wget http://cmdln.googlecode.com/files/cmdln-1.1.2.zip
  unzip cmdln-1.1.2.zip
  cd cmdln-1.1.2
  sudo python setup.py install


Install Perl dependencies
-------------------------

For the MirrorBrain scanner, which is written in Perl, install the following Perl modules that it requires::

  sudo apt-get install libconfig-inifiles-perl libwww-perl libdbd-pg-perl libdatetime-perl libdigest-md4-perl


Build, install, and configure Apache2 modules
---------------------------------------------

MirrorBrain requires several Apache modules, several of which must be built manually. Apache modules are built and installed using :program:`apxs2`. (APache eXtenSion tool)  Apxs2 is in the ``apache2-threaded-dev`` package::

  sudo apt-get install apache2-threaded-dev


Install and configure mod_geoip
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

mod_geoip is available as a prebuilt package::

  sudo apt-get install libapache2-mod-geoip libgeoip-dev

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
  sudo apt-get install gzip
  gunzip GeoIP.dat.gz
  sudo mkdir /var/lib/GeoIP
  sudo cp GeoIP.dat /var/lib/GeoIP/GeoIP.dat

Enable module::

  sudo a2enmod geoip

Restart Apache::

  sudo /etc/init.d/apache2 restart


Install and configure mod_form
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

mod_form must be build from scratch. Download the source from here::

  wget http://apache.webthing.com/svn/apache/forms/mod_form.c
  wget http://apache.webthing.com/svn/apache/forms/mod_form.h

Build, install and activate mod_form (all done in one step, if ``-cia`` is used)::

  sudo apxs2 -cia mod_form.c

Create loader::

  sudo sh -c "cat > /etc/apache2/mods-available/form.load << EOF
  LoadModule form_module /usr/lib/apache2/modules/mod_form.so
  EOF
  "

Enable module::

  sudo a2enmod form

Restart Apache::

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
      # note that the connection string (which is passed straight through to
      # PGconnectdb in this case) looks slightly different - pass vs. password
      DBDParams 'host=localhost user=mirrorbrain password=12345 dbname=mirrorbrain connect_timeout=15'
   </IfModule>
  EOF
  "


Enable module::

  sudo a2enmod dbd

Restart Apache::

  sudo /etc/init.d/apache2 restart


Install and configure mod_mirrorbrain
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Build mod_mirrorbrain::

  sudo apxs2 -cia mod_mirrorbrain.c

Create module loader::

  sudo sh -c "cat > /etc/apache2/mods-available/mirrorbrain.load << EOF
  LoadModule mirrorbrain_module /usr/lib/apache2/modules/mod_mirrorbrain.so
  EOF
  "


Enable module::

  sudo a2enmod mirrorbrain

Restart Apache::

  sudo /etc/init.d/apache2 restart



Build and install helper programs
---------------------------------

Build and install :program:`geoiplookup`::

  cd tools
  gcc -Wall -lGeoIP -o geoiplookup_continent geoiplookup_continent.c
  sudo cp geoiplookup_continent /usr/bin/geoiplookup_continent

Install the :program:`scanner`::

  sudo cp ../tools/scanner.pl /usr/bin/scanner


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


Import initial mirrorbrain data
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Import the table structure, and initial data::

  psql -U mirrorbrain -f sql/schema-postgresql.sql mirrorbrain
  psql -U mirrorbrain -f sql/initialdata-postgresql.sql mirrorbrain


Create needed users and groups
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Create user and group ``mirrorbrain``::

  sudo groupadd -r mirrorbrain
  sudo useradd -r -g mirrorbrain -s /bin/bash -c "MirrorBrain user" -d /home/mirrorbrain mirrorbrain


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

  ./mirrordoctor.py


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


Enable the site::

  sudo a2ensite mirrorbrain


Restart Apache::

  sudo /etc/init.d/apache2 restart


