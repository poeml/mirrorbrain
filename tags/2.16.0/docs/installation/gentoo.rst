Installation on Gentoo Linux
================================================================================

These are very rough notes, which might be helpful for someone as a start maybe.

.. note::
   See also
   http://wiki.github.com/ramereth/ramereth-overlay/gentoo-mirrorbrain-howto
   (which should be merged with this documentation)


.. warning:: 
   When I (Peter) took the following notes, it was my first experience in
   setting up a Gentoo system.

Because I initially emerged git, lots of Perl modules which are be neded later
might already be on the system.

curl will be useful for testing later::

  emerge curl

Build and start the PostgreSQL server::

  emerge postgresql
  emerge --config =postgresql-8.1.11
  su - postgres
  pg_ctl -D /var/lib/postgresql/data start


Build GeoIP::

  emerge geoip


Edit :file:`/etc/make.conf` and add the following settings::

  USE="postgres"
  APACHE2_MPMS="worker"
  APACHE2_MODULES="actions alias auth_basic authn_default authn_file authz_host autoindex dir env expires headers include info log_config logio mime mime_magic negotiation rewrite setenvif status userdir dbd"

Build and start the apache server::

  emerge --newuse apache
  rc-update add apache2 default


Add Lance's overlay to :file:`/etc/make.conf`::

  PORTDIR_OVERLAY="/usr/portage/local/ramereth-overlay"
  mkdir /usr/portage/local
  cd /usr/portage/local
  git clone git://github.com/ramereth/ramereth-overlay.git


Create/edit the file :file:`/etc/portage/package.keywords` and add the following::

  ~www-misc/mirrorbrain-9999
  ~www-misc/mirrorbrain-2.8.1
  ~www-misc/mirrorbrain-2.8
  ~www-apache/mod_mirrorbrain-2.8.1
  ~www-apache/mod_form-132
  ~www-apache/mod_autoindex_mb-2.8.1
  
  ~dev-python/cmdln-1.1.2
  ~dev-python/sqlobject-0.10.4
  ~dev-python/formencode-1.2.1


Due to lack of a dependency in dev-python/cmdln, you need to do::

  emerge unzip

Due to another little lack of a dependency, you also need to do::

  emerge DateTime


Now, you can build mirrorbrain (and its components)::

  emerge -va mirrorbrain

You should get about the following output::

  These are the packages that would be merged, in order:
  
  Calculating dependencies... done!
  [ebuild  N    ] dev-python/psycopg-2.0.8  USE="-debug -doc -examples -mxdatetime" 243 kB [0]
  [ebuild  N    ] www-apache/mod_form-132  0 kB [1]
  [ebuild  N    ] dev-perl/HTML-Tagset-3.20  8 kB [0]
  [ebuild  N    ] dev-python/setuptools-0.6_rc9  247 kB [0]
  [ebuild  N    ] perl-core/Test-Simple-0.80  80 kB [0]
  [ebuild  N    ] perl-core/Sys-Syslog-0.27  75 kB [0]
  [ebuild  N    ] perl-core/Storable-2.18  174 kB [0]
  [ebuild  N    ] dev-perl/Config-IniFiles-2.39  USE="-test" 38 kB [0]
  [ebuild  N    ] dev-python/cmdln-1.1.2  86 kB [1]
  [ebuild  N    ] dev-perl/Digest-MD4-1.5  29 kB [0]
  [ebuild  N    ] www-apache/mod_autoindex_mb-2.8.1  302 kB [1]
  [ebuild  N    ] virtual/perl-Test-Harness-3.10  0 kB [0]
  [ebuild  N    ] dev-perl/Net-Daemon-0.43  28 kB [0]
  [ebuild  N    ] dev-perl/HTML-Parser-3.60  USE="-test" 86 kB [0]
  [ebuild  N    ] dev-python/formencode-1.2.1  USE="-doc" 187 kB [0]
  [ebuild  N    ] virtual/perl-Test-Simple-0.80  0 kB [0]
  [ebuild  N    ] virtual/perl-Sys-Syslog-0.27  0 kB [0]
  [ebuild  N    ] virtual/perl-Storable-2.18  0 kB [0]
  [ebuild  N    ] dev-python/sqlobject-0.10.4  USE="postgres -doc -firebird -mysql -sqlite" 253 kB [0]
  [ebuild  N    ] dev-perl/HTML-Tree-3.23  119 kB [0]
  [ebuild  N    ] dev-perl/PlRPC-0.2020-r1  18 kB [0]
  [ebuild  N    ] dev-perl/DBI-1.601  484 kB [0]
  [ebuild  N    ] dev-perl/DBD-Pg-1.49  144 kB [0]
  [ebuild  N    ] www-apache/mod_mirrorbrain-2.8.1  USE="-memcache" 0 kB [1]
  [ebuild  N    ] dev-perl/Crypt-SSLeay-0.57  121 kB [0]
  [ebuild  N    ] dev-perl/libwww-perl-5.805  USE="ssl" 232 kB [0]
  [ebuild  N    ] www-misc/mirrorbrain-2.8.1  0 kB [1]
  
  Total: 27 packages (27 new), Size of downloads: 2,948 kB
  Portage tree and overlays:
   [0] /usr/portage
   [1] /usr/portage/local/ramereth-overlay
  
  Would you like to merge these packages? [Yes/No]
  ...


Next steps
----------

From here, follow on with :ref:`initial_configuration`.
