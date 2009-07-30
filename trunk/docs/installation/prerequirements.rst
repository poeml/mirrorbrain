.. _prerequirements:

Prerequirements
===============

General
-------

The file tree to be served needs to be accessible locally by the Apache that
runs mod_mirrorbrain. (See `FAQ`_.)

The hardware needs are mediocre; MirrorBrain needs few resources.

.. _`FAQ`: http://mirrorbrain.org/faq/#does-a-copy-of-the-mirrored-content-have-to-be-kept-locally


Apache
------

A recent enough version of the Apache HTTP server is required. **2.2.6** or
later should be used. In addition, the apr-util library needs to be **1.3.0**
or newer. This is because the `DBD database pool functionality`_ was developed
mainly around 2006 and 2007, and reached production quality at the time. This
will mean that you have to upgrade Apache when installing on an oldish
enterprise platform.

.. _`DBD database pool functionality`: http://apache.webthing.com/database/


Status of Apache version on individual platforms
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

openSUSE/SLE
    sufficiently new, since openSUSE 11.0 and SLE11. SLE11 does not ship
    the required PostgreSQL database adapter. The one from openSUSE 11.1 would work.
    Current and stable Apache for openSUSE/SLE can always be found here:
    http://download.opensuse.org/repositories/Apache/

Gentoo
    has a new enough Apache.

CentOS 5/RHEL
    very old Apache. It *might* work or not, but if not, you'll need to get a
    newer one.

Debian
    the Apache in *Etch* is too old. In other releases, it should be alright.


Frontend (mod_mirrorbrain, the redirector)
------------------------------------------

* `mod_geoip`_ and `libGeoIP`_

* `mod_form`_

* if you want to compile with the optional memcache support (there
  should not be reason for it, though), you would need
  libapr_memcache, `mod_memcache`_, `memcache`_ daemon

.. _`mod_form`: http://apache.webthing.com/mod_form/
.. _`mod_geoip`: http://www.maxmind.com/app/mod_geoip
.. _`libGeoIP`: http://www.maxmind.com/app/c
.. _`mod_memcache`: http://code.google.com/p/modmemcache/
.. _`memcache`: http://www.danga.com/memcached/


Database
--------

* `PostgreSQL`_

* mod_mirrorbrain, the core of MirrorBrain, isn't actually bound to a particular
  database; you could use MySQL just as well, SQLite, or Oracle - everything that the 
  Apache DBD API has a driver for. The admin framework and tool set
  however is currently provided for PostgreSQL only.

* `mod_asn`_ is optional, for refined mirror selection and full exploitation 
  of network locality. It works only with PostgreSQL as database. It needs a data 
  type for PostgreSQL called "ip4r". Confer to `its documentation`_ and `its prerequirements`_).

.. _`PostgreSQL`: http://www.postgresql.org/
.. _`mod_asn`: http://mirrorbrain.org/mod_asn/
.. _`its documentation`: http://mirrorbrain.org/mod_asn/docs/
.. _`its prerequirements`: http://mirrorbrain.org/mod_asn/docs/installation/#prerequirements


For the admin tools
-------------------

The toolset for database maintenance needs Python (an old 2.4.x is sufficient) and the following Python modules: 

* :mod:`cmdln`
* :mod:`sqlobject`
* :mod:`psycopg2`

The mirror scanner needs Perl and the following modules:

* :mod:`Config::IniFiles`
* :mod:`libwww::perl`
* :mod:`DBD::Pg`
* :mod:`Digest::MD4`
* :mod:`Date::Parse`


