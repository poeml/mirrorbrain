
.. _initial_configuration:

Initial configuration steps on all platforms
============================================

Create mirrorbrain.conf
~~~~~~~~~~~~~~~~~~~~~~~

Create a configuration file named :file:`/etc/mirrorbrain.conf` with the content below::

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
  

.. note::
   The database password in the above template is only a placeholder and you
   need to edit it: change it to the actual password, the one that you gave
   when you ran PostgreSQL's :program:`createuser` command. Likewise, make sure
   that you picked the same username.

Set the following permissions and privileges on the file::

  sudo chmod 0640 /etc/mirrorbrain.conf 
  sudo chown root:mirrorbrain /etc/mirrorbrain.conf



* now you should be able to type 'mb list' without getting an error.
  It'll produce no output, but exit with 0. If it gives an error, something is
  wrong.



Other possible options per MirrorBrain instance are:

.. describe:: scan_top_include

   Directory names separated by spaces. Meaning: Scan only these directories,
   and ignore all other directories at the top level.

.. describe:: scan_exclude_rsync

   Exclude list for rsync scans (same rules as for rsyncs option ``--exclude``
   apply). Meaning: Ignore all directories or path names that match, everywhere
   in the tree.

.. describe:: scan_exclude

   Exclude list for FTP scans. Meaning: Ignore all directories or path names
   that match, everywhere in the tree.


Test the database admin tool
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If the following command returns no error, but rather displays its usage info,
the installation should be quite fine::

  mb help


Creating some mirrors
~~~~~~~~~~~~~~~~~~~~~

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




  * mirror monitoring needs to be configured. Put this into /etc/crontab::

      -* * * * *                mirrorbrain   mirrorprobe

    configure mirror scanning::

      45 * * * *                mirrorbrain   mb scan -j 3 -a

    another cron job can remove unreferenced files from the database::

      # Monday: database clean-up day...
      30 1 * * mon              mirrorbrain   mb db vacuum



TODO: describe how to test that the install was successful
    (When testing, consider any excludes that you configured, and which might
    introduce confusion.)

* Many HTTP clients can be used for testing, but `cURL`_ is a most helpful tool
  for that. Here are some examples.

  Showy the HTTP response code and the Location header pointing to the new location::

    curl -sI <url>

  Display the metalink::

    curl -s <url>.metalink

  Show a HTML list with the available mirrors::

    curl -s <url>?mirrorlist

.. _`cURL`: http://curl.haxx.se/


TODO: describe a decent logging setup


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
        MirrorBrainMetalinkHashesPathPrefix /srv/hashes/srv/opensuse

    * you need to create a directory where to store the hashes. For instance,
      :file:`/srv/hashes/srv/opensuse`. Note that the full pathname to
      the filetree (``/srv/opensuse``) is part of this target path.
      
      Make the directory owned by the ``mirrorbrain`` user.

    * now, create the hashes with the following command. It is best run as
      unprivileged user (``mirrorbrain``)::

        mb makehashes /srv/opensuse -t /srv/hashes/srv/opensuse


    * add the hashing command to /etc/crontab to be run every few hours. Alternatively, run
      it after changes in the file tree happen.




Create a DNS alias for your web host, if needed



Configure GeoIP
----------------------------------------------

Edit /etc/apache2/conf.d/mod_geoip.conf::

  <IfModule mod_geoip.c>
     GeoIPEnable On
     GeoIPDBFile /var/lib/GeoIP/GeoIP.dat
     #GeoIPOutput [Notes|Env|All]
     GeoIPOutput Env
  </IfModule>

(Change GeoIPOutput All to GeoIPOutput Env)

Note that a caching mode like MMapCache needs to be used, when Apache runs with
the worker MPM.In this case, use::

  <IfModule mod_geoip.c>
     GeoIPEnable On
     GeoIPDBFile /var/lib/GeoIP/GeoIP.dat MMapCache
     GeoIPOutput Env
  </IfModule>



.. configure GeoIP database updates

Seting up automatic updates of the GeoIP database
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

New versions of the GeoIP database are released each month. You can set up a
cron job to automatically fetch new updates as follows. If you do that, make
sure to set the GeoIPDBFile path (see above) to
:file:`/var/lib/GeoIP/GeoLiteCity.dat.updated`::

  # update GeoIP database on Mondays
  31 2 * * mon   root    sleep $(($RANDOM/1024)); /usr/bin/geoip-lite-update





  * configure mod_mirrorbrain.
    You probably want to reate a vhost (e.g.
    /etc/apache2/vhosts.d/samba.mirrorbrain.org.conf) and add the MirrorBrain
    configuration like shown here::

      <VirtualHost your.host.name:80>
          ServerName samba.mirrorbrain.org
      
          ServerAdmin webmaster@example.org
      
          DocumentRoot /srv/samba/pub/projects
      
          ErrorLog     /var/log/apache/samba.mirrorbrain.org/logs/error_log
          CustomLog    /var/log/apache/samba.mirrorbrain.org/logs/access_log combined

          <Directory /srv/samba/pub/projects>
              MirrorBrainEngine On
              MirrorBrainDebug Off
              FormGET On
              MirrorBrainHandleHEADRequestLocally Off
              MirrorBrainMinSize 2048
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

    


Create a virtual host
---------------------

The following snippet would create a new site as virtual host::

  sudo sh -c "cat > /etc/apache2/sites-available/mirrorbrain << EOF
  <VirtualHost 127.0.0.1>
      ServerName mirrors.example.org
      ServerAdmin webmaster@example.org
      DocumentRoot /var/www/downloads
      ErrorLog     /var/log/apache2/mirrors.example.org/error.log
      CustomLog    /var/log/apache2/mirrors.example.org/access.log combined
      <Directory /var/www/downloads>
          MirrorBrainEngine On
          MirrorBrainDebug Off
          FormGET On
          MirrorBrainHandleHEADRequestLocally Off
          MirrorBrainMinSize 2048
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


Restart Apache, best while watching the error log::

  sudo tail -f /var/log/apache2/error.log &
  sudo /etc/init.d/apache2 restart



