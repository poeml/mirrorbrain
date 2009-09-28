

Installation on openSUSE Linux or SLE
-------------------------------------

Add the needed repositories (use the subdirectory matching your distribution):

* http://download.opensuse.org/repositories/Apache:/MirrorBrain/
* http://download.opensuse.org/repositories/devel:/languages:/python/
* http://download.opensuse.org/repositories/server:/database:/postgresql/

You can do this via commandline (we are using openSUSE 11.1 in our example)::

  zypper ar http://download.opensuse.org/repositories/Apache:/MirrorBrain/Apache_openSUSE_11.1 Apache:MirrorBrain 
  zypper ar http://download.opensuse.org/repositories/devel:/languages:/python/openSUSE_11.1 devel:languages:python 
  zypper ar http://download.opensuse.org/repositories/server:/database:/postgresql/openSUSE_11.1 server:database:postgresql

Here's a list of packages needed to have one host running the database and the redirector:

  apache2 apache2-mod_asn apache2-mod_geoip apache2-mod_mirrorbrain
  apache2-mod_form apache2-worker GeoIP libapr-util1-dbd-pgsql
  libGeoIP1 mirrorbrain mirrorbrain-scanner mirrorbrain-tools
  perl-Config-IniFiles perl-DBD-Pg perl-Digest-MD4 perl-libwww-perl perl-TimeDate 
  postgresql postgresql-ip4r postgresql-server python-cmdln python-psycopg2
  python-sqlobject

.. note:: If the web server is set up on a separate host than the database
          server, the web server needs only the package libapr-util1-dbd-pgsql
          and no other postgresql* packages.

You can install the packages via the following commandline::

  zypper install apache2-worker \
  apache2-mod_asn apache2-mod_mirrorbrain \
  postgresql-server postgresql-ip4r \
  mirrorbrain mirrorbrain-scanner mirrorbrain-tools 

The packages not mentioned in this commandline are drawn in via package
dependencies.


.. Configure GeoIP
.. ----------------------------------------------
.. 
.. Edit /etc/apache2/conf.d/mod_geoip.conf:
.. 
.. <IfModule mod_geoip.c>
..    GeoIPEnable On
..    GeoIPDBFile /var/lib/GeoIP/GeoIP.dat
..    #GeoIPOutput [Notes|Env|All]
..    GeoIPOutput Env
.. </IfModule>
.. 
.. (Change GeoIPOutput All to GeoIPOutput Env)
.. 
..         Note that a caching mode like MMapCache needs to be used, when Apache runs with the worker MPM.In this case, use
.. 
..         <IfModule mod_geoip.c>
.. [50px-]    GeoIPEnable On
..            GeoIPDBFile /var/lib/GeoIP/GeoIP.dat MMapCache
..            GeoIPOutput Env
..         </IfModule>



