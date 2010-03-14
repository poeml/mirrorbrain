

Installation on openSUSE Linux or SLE
=====================================

Adding package repositories
---------------------------

Add the needed repositories, using the subdirectory that matches your
distribution version:

* http://download.opensuse.org/repositories/Apache:/MirrorBrain/
* http://download.opensuse.org/repositories/devel:/languages:/python/
* http://download.opensuse.org/repositories/server:/database:/postgresql/

You can do this via commandline (we are using openSUSE 11.1 in our example)::

  zypper ar http://download.opensuse.org/repositories/Apache:/MirrorBrain/Apache_openSUSE_11.1 Apache:MirrorBrain 
  zypper ar http://download.opensuse.org/repositories/devel:/languages:/python/openSUSE_11.1 devel:languages:python 
  zypper ar http://download.opensuse.org/repositories/server:/database:/postgresql/openSUSE_11.1 server:database:postgresql


Installing the MirrorBrain packages
-----------------------------------

Here's a list of packages needed to have one host running the database and the
redirector:

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


Next steps
----------

From here, follow on with :ref:`initial_configuration`.
