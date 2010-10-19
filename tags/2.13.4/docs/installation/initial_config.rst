
.. _initial_configuration:

Initial configuration steps on all platforms
============================================


.. _initial_configuration_file_tree:

Creating a file tree
--------------------

If you haven't got a file tree yet, you should create it now.

Make the directory for the file tree and fill it::

  mkdir /srv/mytree
  rsync .... /srv/mytree

Note that this file tree is a necessary prerequisite to running MirrorBrain,
even though it intercepts the requests to those files and redirects them to
mirrors.

Having said that, there *is* a way to get by without local files, which is by
using the :program:`null-rsync` (found `in the source tree 
<http://svn.mirrorbrain.org/viewvc/mirrorbrain/trunk/tools/null-rsync?view=markup>`_) 
tool instead of
:program:`rsync` to pull the files. :program:`null-rsync` is used exactly as
rsync, but it will create a pseudo file tree that requires very few local
space. However, since those files are filled with zeroes (!), it is important
to make sure that MirrorBrain *never* delivers content from those files. That
is achieved by using the ``MirrorBrainFallback`` directive to define some
mirrors that are *always* available and are guaranteed to have *all* those
files. (The directive can be configured individually per directory in Apache
config.) See the `2.11.0 release notes`_ for details.

Note that if you *do* have the *real* files locally, you can automatically maintain
cryptographic hashes of them in the database; running with pseudo files cuts on
some very useful features. In addition, the local files are always available to
deliver them directly, which is a good fallback behaviour for files that are
not mirrored at all, files that have not arrived on any mirror just yet, and so
on. Of course, you can also make sure that files are never delivered from the
redirector (in other words, it redirects always).

.. note::
   In summary: a tree with real files is required, if you want to serve any
   hashes, zsync, or torrents. But you can make sure that the content is always
   redirected.  The "fake tree" that you can create with null-rsync is good
   *only* for pure redirection. (And Metalinks without hashes.) The server
   doesn't know any content then; only file path, size, mtime, nothing else.

.. _`2.11.0 release notes`: http://mirrorbrain.org/docs/changes/#release-2-11-0-r7896-dec-2-2009



Creating mirrorbrain.conf
-------------------------

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




Testing the database admin tool
-------------------------------

At this point, you should be able to type the following command  without getting an
error::

  mb list
  
It'll produce no output, but exit with 0. If it gives an error, something is
wrong.

.. note:: 
   Do this to verify that the previous steps have been completed successfully.

Likewise, the following command should not return any error, but rather
displays its usage info. If so, the installation should be quite fine::

  mb help


Also, the following should work (you might have to change the path to 
:file:`/usr/share/GeoIP` for your system)::

   % geoiplookup_continent -f /var/lib/GeoIP/GeoIP.dat www.slashdot.org
  NA

The ``NA`` stands for North America and indicates that the GeoIP lookup works
correctly.


Creating some mirrors
---------------------

Collect a list of mirrors (their HTTP baseurl, and their rsync or FTP baseurl
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


See the output of :program:`mb help` for more commands. Refer to
:ref:`maintaining_the_mirror_database` for a full reference documentation to
the :program:`mb` tool.



Setting up required cron jobs
-----------------------------

Setting up mirror monitoring
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Mirror monitoring needs to be set up to run automatically. Put this into
:file:`/etc/crontab`:

The following cron job is needed to check which mirrors are reachable. This
command is responsible for checking the mirrors in short intervals, and marking
them online/offline in the database::

  * * * * *                 mirrorbrain   mirrorprobe

Setting up mirror scanning
~~~~~~~~~~~~~~~~~~~~~~~~~~

Configure mirror scanning::

  45 * * * *                mirrorbrain   mb scan --quiet --jobs 4 --all

Use more parallel scanners (``-j|--jobs ...``) if you have a beefy machine.

The ``--quiet`` option can be used twice (e.g. as ``-qq``), which will totally
silence the scanner, except for error messages. This means that you get a mail
only when there is something wrong.


Maintenance
~~~~~~~~~~~

Another cron job is useful to remove unreferenced files from the database::

  # Monday: database clean-up day...
  30 1 * * mon              mirrorbrain   mb db vacuum


Keeping the GeoIP database uptodate
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The GeoIP database is changed at least once a month, so a new copy should be
downloaded regularly::

  # update GeoIP database on Mondays
  31 2 * * mon              root  sleep $(($RANDOM/1024)); /usr/bin/geoip-lite-update

(The 'sleep' is there so you can copy the line, don't need to adjust the time,
and still the GeoIP servers will not get a lot of simultaneous hits at exactly
the same time. That's all.)



Testing
-------

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


.. _initial_configuration_logging_setup:

Setting up logging
------------------

You may want to log more details than Apache normally logs into the access_log
file. You can define a new log format that gives you an access_log, with
details from MirrorBrain added::

  LogFormat "%h %l %u %t \"%r\" %>s %b \"%{Referer}i\" \"%{User-Agent}i\" \
  want:%{WANT}e give:%{GIVE}e r:%{MB_REALM}e %{X-MirrorBrain-Mirror}o \
  %{MB_CONTINENT_CODE}e:%{MB_COUNTRY_CODE}e ASN:%{ASN}e P:%{PFX}e \
  size:%{MB_FILESIZE}e %{Range}i" combined_redirect



This defines a new log format called "combined_redirect", which you can use in
your virtual hosts with the CustomLog directive. 

Instead of::

  CustomLog /var/log/apache2/myhost/access_log combined

you would use::

  CustomLog /var/log/apache2/myhost/access_log combined_redirect

.. TODO: describe a good logging setup with cronolog



.. _creating_hashes:

Creating hashes
---------------

First, add some configuration::

  MirrorBrainMetalinkPublisher "openSUSE" http://download.opensuse.org

You need to create a directory where to store the hashes. For instance,
:file:`/srv/hashes/srv/opensuse`. Note that the full pathname to the filetree
(``/srv/opensuse``) is part of this target path.
      
Make the directory owned by the ``mirrorbrain`` user.

Now, create the hashes with the following command. It is best run as
unprivileged user (``mirrorbrain``)::

  mb makehashes /srv/opensuse -t /srv/hashes/srv/opensuse

Add the hashing command to /etc/crontab to be run every few hours.
Alternatively, run it after changes in the file tree happen, coupled to some
trigger etc.

(This command was called ``metalink-hasher`` in previous releases of
MirrorBrain.)

.. TODO: show how to run this command (and others) under withlock


Optional things you might want
------------------------------

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



Configuring GeoIP
-----------------

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




Creating a virtual host
-----------------------

Maybe create a DNS alias for your web host, if needed.

.. note::
   A complete reference of all Apache directives can be found `here
   <http://svn.mirrorbrain.org/viewvc/mirrorbrain/trunk/mod_mirrorbrain/mod_mirrorbrain.conf?view=markup>`_.

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

Another example::

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



Make the log directory for the virtual host::

  sudo mkdir /var/log/apache2/mirrors.example.org/


Enable the site::

  sudo a2ensite mirrorbrain

Restart Apache, best while watching the error log::

  sudo tail -f /var/log/apache2/error.log &
  sudo /etc/init.d/apache2 restart



