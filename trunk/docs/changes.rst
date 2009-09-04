.. _release_notes:

Release Notes/Change History
============================


Release 2.10.0 (Sep 4, 2009)
----------------------------

* The cache of metalink hashes, as created by the :program:`metalink-hasher`,
  was changed to more reliably detect changes in the origin files. So far, the
  file modification time was the criterion to invalidate cached hashes. When
  files were replaced with *older* versions (version with smaller mtime), this
  wasn't detected, and a cached hash would not be correctly invalidated.
  https://bugzilla.novell.com/536495 reports this of being an issue.
  
  To fix this, the cache now also uses the file inode as criterion.

  :program:`mod_mirrorbrain` was updated to use the new inode-wise metalink
  hashes is used. At the same time, it knows how to use the previous scheme as
  fallback. 
  
  Thus, the transition should be seamless, and no special steps should be
  required when upgrading. Note however that all hashes are regenerated, which
  could take a while for large file trees, and which could lead to cron jobs
  stacking up.


* There were a number of enhancements, and small bug fixes, in the
  :program:`mb` tool (and accompanying Python module):

  - :program:`mb new`:
  
    - When adding new mirrors, the hostname part in the HTTP base URL might
      contain a port number. This is now recognized correctly, so the DNS
      lookup, GeoIP lookup and ASN lookup for the hostname string can work.
    - The commandline options ``--region-only``, ``--country-only``,
      ``--as-only``, ``--prefix-only`` were added, each setting the respective
      flag.
    - The commandline options ``--operator-name`` and ``--operator-url`` were
      added.
    - The ``--score`` option is depreciated, since it has been renamed it to
      ``--prio``.
  
  - :program:`mb scan`:
  
    - The passing of arguments to the scanner script was fixed in the case
      where the ``-j`` (``--jobs``) option was used together with mirror
      identifier specified on the commandline.

  - :program:`mb list`:

    - Command line options to display the boolean flags were added:
      ``--region-only``, ``--country-only``, ``--as-only`` and
      ``--prefix-only``.

  - :program:`mb scan` / :program:`mb file ls --probe`:

    - the lookup whether the :mod:`multiprocessing` or :mod:`processing` module
      exist was fixed: it could print a false warning that none of them was
      installed.

* The :program:`mirrorprobe` program no longer logs to the console (stderr).
  This allows for running the script without redirection its output to
  :file:`/dev/null` — which could mean swallowing important errors in the end.

  A scenario was documented where the mirrorprobe could fail on machines with
  little memories and many mirrors to check. The fix is to properly set ulimit
  to allow a large enough stack size.

  Error handling was cleaned up; more errors are handled (e.g. socket timeouts
  during response reading) and logged properly; and for exceptions yet
  unhandled, info about the mirror that caused them is printed.


Release 2.9.2 (Aug 21, 2009)
----------------------------

* Most work happened on the documentation, which includes 

  - more installation instructions, 
  - directions for upgrading, 
  - some tuning hints,
  - a quite complete walkthrough through the usage of the :program:`mb`
    commandline tool to maintain the mirror database,
  - instructions how to set up change notifications (:ref:`export_subversion`)
  - list of known problems, and these release notes.

  The documentation is in the :file:`docs` subdirectory, as well as online at
  http://mirrorbrain.org/docs/.

  Notably, there is a new section :ref:`hacking_the_docs`, which explains *how*
  to work on the docs.

* New features:

  - :program:`mb export` can now generate a `mirmon
    <http://people.cs.uu.nl/henkp/mirmon/>`_ mirror list. Thus, it is easy to
    deploy mirmon, automatically scanning the mirrors that are in the database.
    See :ref:`export_mirmon` for usage info.
  - In :program:`mod_autoindex_mb`, displaying the "Mirrors" and "Metalink"
    links was implemented for configurations with Apache's ``IndexOptions
    HTMLTable`` configured.

* Two minor bugs were fixed:

  - Missing slash added in :program:`mod_autoindex_mb` to terminate the XHTML
    ``br`` element in the footer.
  - The scanner now ignores rsync temp directories (:file:`.~tmp~`) also when
    they occur at the top level of the tree, and not below.


Release 2.9.1 (Jul 30, 2009)
----------------------------

* :program:`mb new`

  - Now an understandable error message is printed when the
    geoiplookup_continent couldn't be executed. Thanks to Daniel Dawidow for
    providing helpful information to track this down.

* :program:`mod_mirrorbrain`

  - Under unusual circumstances it may happen that mod_mirrorbrain can't
    retrieve a prepared SQL statement. This occurs when an identical database
    connection string is being used in different virtual hosts. To ease
    tracking down this special case, the module now logs additional information
    that could be useful for debugging. Also, it logs a hint noting that
    connection strings defined with DBDParams must be unique, and identical
    strings cannot be used in two virtual hosts.

* The :program:`mod_mirrorbrain` example configuration files were updated to
  reflect several recent (or not so recent) changes:

  - the switch to PostgreSQL
  - the now disabled memcache support
  - the updated GeoIP database path (/var/lib/GeoIP instead of /usr/share/GeoIP)


Release 2.9.0 (Jul 28, 2009)
----------------------------

* A very hindering restriction in the :program:`mb` tool which made it require
  `mod_asn <http://mirrorbrain.org/mod_asn/>`_ to be installed alongside
  MirrorBrain has been removed. MirrorBrain can now be installed without
  installing mod_asn.

* The Subversion repository was moved to 
  http://svn.mirrorbrain.org/svn/mirrorbrain/trunk/.

* rsync authentication was fixed. Credentials given in rsync URLs in the form of
  ``rsync://<username>:<password>@<host>/<module>`` now work as expected. Patch
  by Lars Vogdt.

* The documentation has been moved into a `docs subdirectory
  <http://svn.mirrorbrain.org/svn/mirrorbrain/trunk/docs/>`_, and is rewritten
  in reStructured Text format, from which HTML is be generated via Sphinx
  (http://sphinx.pocoo.org/). Whenever the documentation is changed in
  subversion, the changes automatically get online on
  http://mirrorbrain.org/docs/

* Parallelized mirror probing.  Note: for this new feature, the Python modules
  :mod:`processing` or :mod:`multiprocessing` need to be installed.  If none of them is
  found, the fallback behaviour is to probe serially, like it was done before.
  This new feature affects the :program:`mb probefile` and :program:`mb file`
  commands, and not actually the mirrorprobe, which has always ran threaded. It
  also affects the scanner (:program:`mb scan`) to speed up the checks done
  when only a subdirectory is scanned.

* Various new features were implemented in the :program:`mb` tool:

  * :program:`mb probefile`
  
    - Implemented downloading (and displaying) of content.
    - A ``--urls`` switch was added, to select the kind of URLs to be probed.
  
      - ``--urls=scan`` probes the URLs that would be used in scanning.
      - ``--urls=http`` probes the (HTTP) base URLs used in redirection.
      - ``--urls=all`` probes all registered URLs.
  
    - The usual proxy environment variables are unset before probing
      (:envvar:`http_proxy`, :envvar:`HTTP_PROXY`, :envvar:`ftp_proxy`, :envvar:`FTP_PROXY`)
    - Report the mirror identifier for FTP socket timeouts
  
  * :program:`mb scan`
  
    - Logging output was considerably improved, avoiding lots of ugly
      messages which look like real errors (and tend to cover real ones)
    - The time that a scan took is now shown. 
  
  * :program:`mb new` 

    - while looking up a mirror's location when a new mirror is added, try
      different geoip database locations (GeoIP database was moved around on
      openSUSE...).  
    - prefer the larger city lite database, if available, and prefer updated
      copies that were fetched with the :program:`geoip-lite-update` tool.

  * :program:`mb list` 

    - add ``--other-countries`` option to allow displaying the
      countries that a mirror is configured to handle in addition to its own
      country

* :program:`mod_mirrorbrain`: in the ``generator`` tag of metalinks, include
  mod_mirrorbrain's version string

* The :program:`metalink-hasher` tool has been revised to implement a number of
  lacking features:

  - Automatic removal of old hashes, which don't have a pendant in
    the file tree anymore, is implemented now.
  - A summary of deletions is printed after a run.
  - A number of things were optimized to run more efficiently on
    huge trees, mainly by eliminating all redundant :func:`stat` calls.
  - sha256 was added to the list of digests to generated.
  - The need to specify the ``-b`` (``--base-dir``) option was eliminated,
    which makes the command easier to use.
  - The order in which the tool works through the todo list of directories
    was changed to be alphabetical.
  - Using a Python :func:`set` builtin type instead of a list can speed up finding
    obsolete files in the destination directory by 10 times, for huge
    directories.
  - The program output and program help was improved generally. 
  - Various errors are caught and/or ignored, like vanishing directories and
    exceptions encountered when recursively removing ignored directories.
  - The indentation of verification containers was corrected, so it looks sane
    in the metalink in the end.
  - The version was bumped to 1.2.


* :program:`geoip-lite-update`: This tool to fetch GeoIP databases has been
  updated to use the path that's used in the openSUSE package since recently
  (:file:`/var/lib/GeoIP`), and which complies better to the Linux Filesystem
  Hierarchy Standard. It still tries the old location (:file:`/usr/share/GeoIP`) as
  well, so to continue to work in a previous setup.


* :program:`mirrorprobe`

  - A logrotate snippet was added.
  - The mirrorprobe logfile was moved to the :file:`/var/log/mirrorbrain/` directory.

* The openSUSE RPM package now creates a user and group named `mirrorbrain`
  upon installation. Also, it packages a runtime directory
  :file:`/var/run/mirrorbrain` (which is cleaned up upon booting) and a log directory
  :file:`/var/log/mirrorbrain`. Some additional Requires have been added, on the
  perl-TimeDate, metalink and libapr-util1-dbd-pgsql packages.



Release 2.8.1 (Jun 5, 2009)
---------------------------

* Python 2.6 compatibility fixes:

  - :program:`mb file ls` ``--md5`` now uses the :mod:`hashlib` module, if
    available (this fixes a DepracationWarning given by Python 2.6 when
    importing the :mod:`md5` module).
  - :program:`mb list`: The ``--as`` option had to be renamed to ``--asn``,
    because ``as`` is a reserved keyword in Python, and Python 2.6 is more strict
    about noticing this also in cases where just used as an attribute.
  - The ``b64_md5`` function was removed, which was no longer used since a while.

* :program:`mb file ls`

  - make the ``--md5`` option imply the ``--probe`` option

* :program:`mb export`

  - when exporting metadata for import into a VCS (version control system),
    handle additions and deletions

* The docs were updated to point to new RPM packages in the openSUSE build service (in
  a repository named `Apache:MirrorBrain <http://download.opensuse.org/repositories/Apache:/MirrorBrain/>`_).
  The formerly monolithic package has been split up into subpackages.

* perl-Config-IniFiles was added to the list of perl packages required by the
  scanner (:program:`mb scan`)


Release 2.8 (Mar 31, 2009)
--------------------------

* Improvements in the scanner, mainly with regard to the definition of
  patterns for files (and directories) that are to be included from scanning.
  Old, hardcoded stuff from the scanner has been removed. Now, excludes can be
  defined in :file:`/etc/mirrorbrain.conf` by the ``scan_exclude`` and
  ``scan_exclude_rsync`` directives. 
  The former takes regular expressions and is effective for FTP and HTTP scans,
  while the latter takes rsync patterns, which are passed directly to the
  remote rsync daemon.
  See http://mirrorbrain.org/archive/mirrorbrain-commits/0140.html for details.
  This can decrease the size of the database (>20% for openSUSE), and for many
  mirrors it considerably shortens the scan time.
* Fixed a bug where the scanner aborted when encountering filenames in (valid
  or invalid) UTF-8 encoding. See https://bugzilla.novell.com/show_bug.cgi?id=490009
* Improved the implementation of exclusions as well as the top-level-inclusion
  pattern, which were not correctly implemented to work in subdir scans. 
* The documentation was enhanced in some places.
* mod_autoindex_mb (which is based on mod_autoindex) was rebased on httpd-2.2.11.
* :program:`mb dirs``: new subcommand for showing directories that the database contains,
  useful to tune scan exclude patterns.
* :program:`mb export``: implement a new output format, named ``vcs``. Can be used to commit
  changes to a subversion repository and get change notifications from it. See 
  http://mirrorbrain.org/archive/mirrorbrain-commits/0152.html
* Partial deletions (for subdir scans) have been implemented.
* :program:`mb list`` accept ``--country`` ``--region`` ``--prefix`` ``--as``
  ``--prio`` options to influence which details are output by it.
* :program:`mb file``: support for probing files, with optional md5 hash check of the
  downloaded content.
* The latter three changes have already been described in more detail at
  http://mirrorbrain.org/news_items/2.7_mb_toolchain_work


Release 2.7 (Mar 4, 2009)
-------------------------

* Completely reworked the file database. It is 5x faster and one third the
  size. Instead of a potentially huge relational table including timestamps (48
  bytes per row), files and associations are now in a single table, using
  smallint arrays for the mirror ids. This makes the table 5x faster and 1/3
  the size. In addition, we need only a single index on the path, which is a
  small and very fast b-tree.  This also gives us a good search, and the chance
  to do partial deletions (e.g. for a subtree).
* With this change, MySQL is no longer supported. The core, mod_mirrorbrain,
  would still work fine, but the toolchain around is quite a bit specific to
  the PostgreSQL database scheme now. If there's interest, MySQL support in the
  toolchain can be maintained as well.
* many little improvements in the toolchain were made.
* Notably, the scanner has been improved to be more efficient and give better
  output.
* mirror choice can be influenced for testing with a query parameter (``as=``),
  specifying the autonomous system number.


Release 2.6 (Feb 13, 2009)
--------------------------

* supports additional, finer mirror selection, based on network
  topological criteria, network prefix and autonomous system number, using
  `mod_asn <http://mirrorbrain.org/mod_asn/>`_ and global routing data.
* updated database schemes and toolchain -- PostgreSQL support is solid now
* work on installation documentation for both MySQL and PostgreSQL
  (the latter is recommended now, because it allows for nifty features in the
  future. The :program:`mb` tool has an :program:`mb export` subcommand now,
  perfect to migrate the database.)
* toolchain work


Release 2.5 (Feb 3, 2009)
-------------------------

* working on PostgreSQL support
* working on the INSTALL documentation
* scanner: 0.22

  - more efficient SQL statement handling
  - output much improved
  - added SQL logging option for debugging

* :program:`mb` (mirrorbrain tool): 

  - bugfix in the :program:`mb file` command: make patterns work which have a
    wildcard as first character.
  - extend :program:`mb scan` to accept ``-v`` and ``--sql-debug`` and pass it
    to the scanner


Release 2.4 (Jan 23, 2009)
--------------------------

* rename :program:`mod_zrkadlo` to :program:`mod_mirrorbrain`
* use `mod_geoip <http://www.maxmind.com/app/mod_geoip>`_ for GeoIP lookups,
  instead of doing it ourselves. We can now use the GeoIP city database for instance
* handle satellite "country" called ``A2``
* auto-reenable dead mirrors
* :program:`geoiplookup_city` added, new tool to show details from GeoIP city databases
* :program:`geoip-lite-update` tool updated, with adjusted URL for GeoLite databases. It
  also downloads the city database now.
* deprecate ``clientip`` query parameter, which can no longer work
  once we use mod_geoip. Implement ``country`` parameter that can be used instead.
* make memcache support optional at compile time


Release 2.3 (Dec 13, 2008)
--------------------------

* add commandline tool to edit marker files. (Marker files are used to generate
  mirror lists. Each marker file is used to determine whether a mirror mirrors
  a certain subtree.)
* improvements and few features in the toolchain:

  - the mirrorprobe now does GET requests instead of HEAD requests.
  - :program:`mb`, the mirrorbrain tool, has a powerful :program:`mb
    probefile` command now that can check for existance of a file on all
    mirrors, probing all URLs. This is especially useful for checking whether
    the permission setup for staged content is correct on all mirrors.

* new database fields: ``public_notes``, ``operator_name``, ``operator_url``
* new database tables: ``country``, ``region``
* generate mirror lists


Release 2.2 (Nov 22, 2008)
--------------------------

* simplified database layout, with additional space save.


Release 2.1 (Nov 9, 2008)
-------------------------

* simplified the Apache configuration: It is no longer needed to configure a
  database query. At the same time it's less error-prone and avoids trouble
  if one forgets to update the query, when the database schema changes. 
* specific mirrors can be now configured to get only requests for files < n bytes


Release 2.0 (Nov 3, 2008)
-------------------------

* implement better fallback mirror selection
* add :program:`mb file` tool to list/add/rm files in the mirror database


Release 1.9 (Oct 26, 2008)
--------------------------

* add bittorrent links (to all .torrent files that are found) into metalinks
* embed PGP signatures (.asc files) into metalinks
* add configurable CSS stylesheet to mirror lists

* :program:`mod_zrkadlo`:

  - implement the redirection exceptions (file too small, mime type not allowed
    to be redirected etc) for transparently negotiated metalinks.
  - add ``Vary`` header on all transparently negotiated resources.
  - allow to use the apache module and all tools with multiple instances of the
    mirrorbrain. Now, one machine / one Apache can host multiple separate
    instances, each in a vhost.

* new, better implementation of rsyncusers tool
* bugfixes in the scanner, mainly for scanning via HTML
* installation instructions updated

* a number of small bugs in the tools were fixed and several improvements
  added.

* added "mirrordoctor", a commandline tool to maintain mirror entries in the
  database. Finally!


Release 1.8 (Jun 2, 2008)
-------------------------

* mod_zrkadlo now uses `mod_memcache <http://code.google.com/p/modmemcache/>`_ for
  the configuration and initialization of memcache
* :program:`metalink-hasher` script added, to prepare hashes for injection into
  metalink files
* :program:`rsyncusers` analysis tool added
* :program:`rsyncinfo` tool added
* scanner bugfix regarding following redirects for large file checks
* failover testbed for text mirrorlists implemented
* metalinks: switch back to RFC822 format
* new ``ZrkadloMetalinkPublisher`` directive 
* fix issue with ``<size>`` element
* now there is another (more natural) way to request a metalink: by appending
  ``.metalink`` to the filename.
* change metalink negotiation to look for :mimetype:`application/metalink+xml` in the
  ``Accept`` header (keep ``Accept-Features`` for now, but it is going to be removed
  probably)


Release 1.7 (Apr 21, 2008)
--------------------------

* new terse text-based mirrorlist
* allow clients to use :rfc:`2295` Accept-Features header to select variants
  (metalink or mirrorlist-txt)
* metalink hash includes can now be out-of-tree
* :program:`mod_autoindex_mb` added
* adding a ``content-disposition`` header



Older changes
-------------

Please refer to the subversion changelog: http://svn.mirrorbrain.org/svn/mirrorbrain/trunk
respectively http://svn.mirrorbrain.org/viewvc/mirrorbrain/trunk/

