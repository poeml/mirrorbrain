.. _release_notes:

Release Notes/Change History
============================


Release 2.14.0 (r8208, Nov 6, 2010)
-----------------------------------

This release brings a number of new features, and also some bug fixes.


- On the precondition that the "GeoLite City" GeoIP database is used,
  MirrorBrain now uses geographical distance as additional criterion in mirror
  selection. This is useful in
  
    1) large countries (like the US) and any countries with many mirrors 
    
    2) countries without mirrors, where only a random mirror from the continent
       could be selected otherwise. (Defining fallback mirrors for the latter
       countries worked before, and still has precedence.) 
  
  To take advantage of this feature, the free `GeoLite City
  <http://www.maxmind.com/app/geolitecity>`_ GeoIP database needs to be used.
  See the `2.14.0 upgrade notes`_ for instructions. (This implements `issue
  34`_.)

- Per-file mirror lists have been improved by showing data in a better readable
  way, and by embedding a link to a Google map showing the 9 closest mirrors.

- When running behind a proxy, prefix detection (for containment in network
  prefixes of mirrors) did not work because mod_mirrorbrain only saw the
  connecting IP address, and didn't look at an address passed via HTTP headers
  from the proxy. This has been fixed. (AS, country and continent comparisons
  already did this.)

- An experimental feature for restricted downloads has been added, by
  redirecting to temporary URLs whose validity need to be verified by the
  mirrors. See
  http://www.mail-archive.com/mirrorbrain@mirrorbrain.org/msg00011.html
  This a prototype implementation that might be changed later, hence the new
  Apache config directive is called
  ``MirrorBrainRedirectStampKey_EXPERIMENTAL`` at the moment.

- The module did not work when access was restricted with authentication (e.g.
  Basic Authentication), due to a broken check which simply needed to be
  removed. (A bit of code inherited from mod_offload, and likely still dating
  back to old Apache 1.3 API.)

- MirrorBrain has been tested (successfully) against the latest
  :program:`zsync` release (0.6.2) and the documentation updated. 

- An optimization in the :func:`find_lowest_rank` function, which is use to
  fetch the prioritized mirror from an array, makes it return immediately when
  the size of the array is 1. This might save some CPU cycles.

Please read the `2.14.0 upgrade notes`_ before upgrading!

.. _`2.14.0 upgrade notes`: http://mirrorbrain.org/docs/upgrading/#to-2.14.0
.. _`issue 34`: http://mirrorbrain.org/issues/issue34


Release 2.13.4 (r8188, Oct 19, 2010)
------------------------------------

This is a maintenance release with improvements in the mirror scan reporting,
and small fixes and improved usability. In addition the documentation were
enhanced and added to in some places.

Noteworthy are the added instructions on setting up automatic GeoIP database
updates (see below).


* :program:`mb scan`:

  - The output of the scanner has been improved, by introducing a
    ``-q|--quiet`` option. Used once, only a summary line per scanned mirror
    will be shown. Used twice, no output will be produced except errors.
  - When a scan via rsync ran into a timeout, the name of the affected
    mirror was not reported. The error message was only "rsync timeout", and
    while there normally were other messages giving a hint, output is now
    improved to include the mirror identifier.
  - When enabling a mirror after successful scanning, the scanner now makes
    sure that the mirror is not only marked "enabled" but also marked being
    "online". Mirrors are normally marked online by the mirrorprobe (which is
    typically run once per minute), but it is much more logical when a mirror
    is really directly available after scanning with ``--enable``.

* :program:`mb scan` and :program:`mirrorprobe`:

  - There was a case of a quirky web server software that ignores requests
    without Accept header. The mirrorprobe and the scanner now send an Accept
    header with value '*/*', because sending this header in general should not
    harm.

* :program:`geoip-lite-update`:

  - This script now works on Ubuntu. It no longer relies on a command named
    :program:`ftp` being capable of doing HTTP downloads, and prefers
    :program:`curl` or :program:`wget` if available.
  - The script is quiet now, producing no output if no error is encountered.

Documentation improvements:

- The logging configuration example has been updated (See
  :ref:`initial_configuration_logging_setup`)
- The instructions to update the GeoIP databases on Ubuntu have been updated.
  (See :ref:`installation_ubuntu_debian`)
- Documentation (for all platforms) about setting up automatic updates of the
  GeoIP database was blatantly missing.
- A possibly disturbing '-' in front of cron examples has been removed, which
  work with Vixie cron but not with Anacron as used by Ubuntu.
- Ubuntu install docs for 10.04 have been updated.
- The example for using the :program:`geoiplookup_continent` tool now shows how
  to specify the path to a GeoIP database.


Release 2.13.3 (r8166, Sep 26, 2010)
------------------------------------

This is a release that fixes two important bugs in the Metalink generator. In
addition, it includes a number of compatibility fixes for Torrents.

* :program:`mod_mirrorbrain`:

  - The Magnet links embedded in Metalinks could cause the Metalink client
    :program:`aria2c` to wait a long time on P2P connections, and not try the
    listed mirrors anymore (`issue 73`_). These links are no longer included at
    the moment, unless ``MirrorBrainMetalinkMagnetLinks On`` is set in the
    Apache configuration.
  - Under the conditions that 

    + an ``Accept`` header with ``application/metalink+xml`` or ``metalink4+xml`` is sent,
    + and the request goes to a path that doesn't exist, 
    + but some extension (``.foo``) could be split off, 
    + and a corresponding path without extension exists, 
      
    mod_mirrorbrain delivered the file matching the path with the extension
    split off, instead of replying with a ``404 Not found``. This affected
    :program:`aria2c` when it requested non-existing files. The bug was found
    and fixed by Michael Schröder and closes `issue 75`_.
  - When generating Torrents, the order of keys was not obeyed, which should be
    lexicographical. This is now the case, so the Torrents should be valid also
    for clients that insist on correct ordering. This should improve the
    compatibility to some clients, notably :program:`rtorrent`. Tracked in
    `issue 74`_ and `issue 78`_.
  - The MD5 sum in Torrent info hashes was wrongly sent in binary form, instead
    of being hex-encoded. In addition, the key was wrongly named ``md5`` while
    ``md5sum`` is the correct name. Fixing `issue 77`_.
  - Not a bugfix, but a hopefully useful addition is that Torrents now contain
    a "created by" key, indicating the generator of the torrent, and the
    version number (e.g. ``MirrorBrain/2.13.3``). Suggested in `issue 65`_.
  
Please read the `2.13.3 upgrade notes`_ before upgrading.

Thanks for all kind help and contribution!

.. _`issue 65`: http://mirrorbrain.org/issues/issue65
.. _`issue 73`: http://mirrorbrain.org/issues/issue73
.. _`issue 74`: http://mirrorbrain.org/issues/issue74
.. _`issue 75`: http://mirrorbrain.org/issues/issue75
.. _`issue 77`: http://mirrorbrain.org/issues/issue77
.. _`issue 78`: http://mirrorbrain.org/issues/issue78
.. _`2.13.3 upgrade notes`: http://mirrorbrain.org/docs/upgrading/#from-2-13-x-to-2-13-3




Release 2.13.2 (r8153, Sep 19, 2010)
------------------------------------

This release adds worthwhile new features to the mirror list generator that
you will enjoy:

* :program:`mod_mirrorbrain`:

  - The content of the mirror lists (details pages) are now wrapped into a
    XHTML/HTML ``DIV`` container with ``id="mirrorbrain-details"``. This
    improves the possibilities for styling in conjunction with a stylesheet
    linked in via the ``MirrorBrainMirrorlistStyleSheet`` directive (`issue
    63`_).

  - Further individual design can now be achieved by specifying the XHTML/HTML
    header and footer which are placed around the page body instead of the
    built-in XHTML (`issue 63`_). This is configured with two new Apache
    configuration directives.

    This is documented here: :ref:`styling_details_pages`.

  - Hashes can now be requested without a filename being included in the
    response, to simplify parsing (`issue 68`_). This is done by sending the
    query string ``only_hash``. This works with different ways to request a
    hash::

      http://host.example.com/foo.md5?only_hash 
      http://host.example.com/foo?md5&only_hash

    Instead of ``99eaed37390ba0571f8d285829ff63fc  du.list``, the server will
    just return ``99eaed37390ba0571f8d285829ff63fc``.

  - The filename in hashes can also be suppressed site-wide (and therewith, on
    the server side) with a new Apache config directive
    ``MirrorBrainHashesSuppressFilenames On``. It goes into virtualhost context.

  - When sending out a hash to a client (as requested by appending e.g.
    ``.md5``), there is now a *double* space between hash and filename -- just
    like as the familiar tools like :program:`md5sum` and :program:`sha1sum` do
    it. This should avoid confusion and extra effort in parsing.

  - The mirror list's content type header now comes with UTF-8 as character
    set, instead of ISO-8859-1, which should make more sense.

* :program:`mb export --format=mirmon`:

  - Exporting a mirror list for `mirmon
    <http://people.cs.uu.nl/henkp/mirmon/>`_ has been adjusted to the default
    in mirmon-2.3 of its option ``list_style=plain``. The other format
    (``list_style=apache``) can also be generated, if mb export is used with
    ``--format=mirmon-apache``. This fixes `issue 62`_.

    The documentation :ref:`export_mirmon` has been updated to reflect this.


.. _`issue 62`: http://mirrorbrain.org/issues/issue62
.. _`issue 63`: http://mirrorbrain.org/issues/issue63
.. _`issue 68`: http://mirrorbrain.org/issues/issue68


Release 2.13.1 (r8136, Sep 18, 2010)
------------------------------------

This is a minor release, adding some improvements and fixing a bug that sneaked
into the last release.

* :program:`mb edit`:

  - A problem was fixed that made it impossible to remove an URL by setting it
    to an empty string. The fix for `issue 30`_ was the culprit. This was a
    regression that came with the last release (2.13.0).

* :program:`mb list/edit/show/...`: 

  - In some situations, the fuzzy-matching on mirror identifiers made it
    impossible to select certain mirrors. Phillip Smith reported this
    issue and submitted a clever patch, which retains the convenient
    behaviour, but also allows for selection mirrors by their full name. 
    This fixes `issue 61`_.
  
* :program:`mb scan`:

  - Scanning lighttpd web servers is now supported. Thanks to patch contributed
    by Phillip Smith. This fixes `issue 60`_.


* Changes regarding packaging:

  - Thanks to the work of Phillip Smith, there are now packages for Arch Linux
    and the ArchServer distribution.

  - On Debian and Ubuntu, the mirrorbrain user and group are now automatically
    created by the package, as well as /var/log/mirrorbrain. This simplifies
    the installation procedure and fixes `issue 4`_.

  - Thanks to the help of Cory Fields, the 2.12 -> 2.13.0 upgrade now works
    seamlessly on Debian/Ubuntu. Fixing `issue 57`_.


.. _`issue 4`: http://mirrorbrain.org/issues/issue4
.. _`issue 30`: http://mirrorbrain.org/issues/issue30
.. _`issue 57`: http://mirrorbrain.org/issues/issue57
.. _`issue 60`: http://mirrorbrain.org/issues/issue60
.. _`issue 61`: http://mirrorbrain.org/issues/issue61



Release 2.13.0 (r8123, Sep 6, 2010)
-----------------------------------

This is a big release, with many new features, and lots of bugs fixed. Big
effort has also been put in to ensure a seamless upgrade. 

Please read the `2.13.0 upgrade notes`_.

New features:

* This release **fully supports IETF Metalinks**, as finalized in :rfc:`5854` early in 2010.
  The extension ``.meta4`` triggers the IETF Metalink response. An HTTP Accept
  header containing ``metalink4+xml`` also elicits this kind of response. This
  closes `issue 14`_. The old (v3) Metalinks are still supported, and
  transparent content negotiation (TCN) is supported with both variants.  

* As the cache of hashes needed to be restructured for this feature, it became
  possible to implement a number of additional features. Inclusion of **various
  metadata in the mirror lists** is supported now (`issue 41`_): 
  
  - file size and modification time
  - SHA256 hash
  - SHA1 hash
  - MD5 hashes
  - BitTorrent infohash
  - link to Metalink
  - link to Torrent
  - zsync link 
  - Magnet link (needs testing)
  - link to PGP signature (if available)

  These metadata pages resp. mirror lists can now be requested by appending
  ``.mirrorlist`` to an URL. The previous way, using a question mark
  (``&mirrorlist``) continues to be supported for backwards compatibility.

* Thus, MirrorBrain is now a feature-rich **hash/metadata server**. A so-called
  "top hash" (cryptographic hash of the complete file) can now be requested.
  Depending on the extension added to the URL, like ``.md5``, ``.sha1``, or
  ``.sha256``, the respective representation is returned. This closes `issue
  42`_.

  Like before, MirrorBrain also stores piece-wise hashes for chunks of the files.
  The chunk size is now configurable via :file:`/etc/mirrorbrain.conf`, see
  :ref:`configuring_torrent_generation`.

  All hashes are now stored in the database. (See
  :ref:`design_database_hash_store` design notes.)

  A fallback mechanism is in place to read existing hashes from disk, if the
  database doesn't have the new hashes yet (useful for the migration period).

* Even though more hashes are calculated, and hashes stored in the database,
  hashing is **twice as fast** as before, not relying the external metalink
  binary any longer. All functionality of the :program:`metalink-hasher` tool
  has been integrated into :program:`mb makehashes`, which makes sure to never
  read data from disk more than once, regardless of how many hashes are
  calculated. 

  The external tool names :program:`metalink` is no longer used, and the
  package dependency on the :program:`metalink` package is no longer there.

* MirrorBrain now has a **torrent generator embedded**. Torrents are generated in
  realtime (from hashes cached in the database). See
  :ref:`configuring_torrent_generation` for details. This resolves `issue 37`_.

* MirrorBrain now has basic **zsync support**. The `zsync distribution method
  <http://zsync.moria.org.uk/>`_ is rsync over HTTP, so to speak, and
  MirrorBrain can generate zsync files on-the-fly. MirrorBrain supports the
  simpler variant which doesn't look into compressed content. It is compatible
  to the current zsync release (0.6.1).

  See :ref:`configuring_zsync_generation` for details.

  This feature is off by default, because Apache allocates large amounts of
  memory for large rows from database; this may be worked around in the future.


* Initial support for `Magnet links <http://magnet-uri.sourceforge.net/>`_.
  This largely closes `issue 38`_, but requires further testing/finetuning. See
  :ref:`magnet_links` for documentation.

* Ubuntu 10.04 (Lucid) support! (`Issue 6`_ had to be fixed for this.)


While these are the main news, there is a number of smaller feature updates to
be listed:

* :program:`mb makehashes`:

  - This is the new tool for hashing files. It supersedes the previously used
    :program:`metalink-hasher` and the external :program:`metalink` tool.
  - :program:`metalink-hasher` is a wrapper now, for backwards compatibility,
    to avoid breaking existing setups.
  - A ``--force`` option has been added to force refreshing existing hashes.
  - The usage example with ``--base-dir`` has been improved.
  
* :program:`mb list`:

  - A new option ``-N|--number-of-files`` has been added, which displays the
    number of files that a mirror is known to have.

    To achieve this, a new stored procedure :func:`mirr_get_nfiles` has been
    implemented, which retrieves this number, given either a mirror id or its
    name. It is added automatically when migrating from previous versions, and
    made available in through the :mod:`mb.core.mirror_get_nfiles` method.
  - ``mb list <mirror identifier>`` did not work due to a missing module import
    in the Python script. This has been amended.

* :program:`mb update`:

  - This command can now also update country & region info in mirror records
    (from GeoIP). Before, it updated only the network prefix and AS number, and
    geographical coordinates. But country and region assignments occasionally
    change as well.
  - A ``--dry-run`` option has been added, to allow seeing the changes before
    applying them.
  - An ``--all`` option has been added, which updates all metadata, same as when
    giving ``-c -a -p --country --region`` all at once.
  - The command now properly takes notice of hostnames that don't resolve in the
    DNS (so further action cannot be taken).

* :program:`mb db sizes`:

  - The output of this command now includes also the size of the new hashes table.

* :program:`mb db vacuum`:

  - The database cleanup now takes into account that files in the filearr table
    might not exist on any mirror, but only locally - so they could be
    referenced in the hash table.

* :program:`mod_mirrorbrain`:

  - There is an additional logging handle which provides details about the
    request and the response. The Apache module takes note in the subprocess
    environment what the client requested and which representation of the file
    was actually sent as response. Those variables can be used for logging with
    standard Apache CustomLog configuration with e.g. ``want:%{WANT}e
    give:%{GIVE}e``.

* :program:`mod_autoindex_mb`:

  - The link "Metalink" is no longer displayed. Instead, the link "Mirrors" has
    been renamed to "Details". 


.. _`issue 6`: http://mirrorbrain.org/issues/issue6
.. _`issue 14`: http://mirrorbrain.org/issues/issue14
.. _`issue 37`: http://mirrorbrain.org/issues/issue37
.. _`issue 38`: http://mirrorbrain.org/issues/issue38
.. _`issue 41`: http://mirrorbrain.org/issues/issue41
.. _`issue 42`: http://mirrorbrain.org/issues/issue42


Bug fixes:

* :program:`mod_mirrorbrain`:

  - When a client IP's network prefix did not match a mirror's network prefix
    exactly, the assignment of the client to this mirror would fail, even
    though the client IP was (also) contained in the mirror's network prefix.
    This has been rectified by properly checking for containment of the IP,
    fixing `issue 52`_.
  - Requests with PATH_INFO were not ignored, as they should be.  The default
    behaviour of Apache is to ignore such requests, and CGI or script handler
    deviate from that. :program:`mod_mirrorbrain` now also correctly returns
    ``404 Not Found`` for such requests. This fixes `issue 18`_, as well as
    `openSUSE bug #546396
    <https://bugzilla.novell.com/show_bug.cgi?id=546396>`_ (which is not
    publicly readable).
  - When the only available mirror(s) had a limitation flag set (such as
    ``region_only``), and a metalink was transparently negotiated, an empty
    metalink would result. This is now prevented, and the file delivered
    directly instead.  Other representations (mirror lists, non-negotiated
    metalinks, torrents, hashes) are generated also if there is no mirror. This
    was tracked in `openSUSE bug #602434
    <https://bugzilla.novell.com/show_bug.cgi?id=602434>`_. The mirrorlist is
    improved when there's no mirror, and can still list all hashes, and give
    the direct download URL.
  - The module now works when the path used in the Apache <Directory> block
    contains symlinks, fixing `issue 17`_.
  - Errors from the database adapter (lower DBD layer) are now resolved to
    strings, where available.
  - Some variable types have been corrected from int to ``apr_off_t``, using
    :func:`apr_atoi64` instead of :func:`atoi`. This applies to: ``min_size``,
    ``file_maxsize``, and the database identifier of a hash row. This at least
    fixes the info message given when a file is excluded from redirection due
    to its size. The checks seemed to work nevertheless, because the
    ``min_size`` numbers were small and ``file_maxsize`` numbers large, which
    helped to get the correct result when comparing.


* :program:`mb scan`:

  - Usage of FTP authentication was fixed (with credentials encoded into the
    URL). The change done in January
    http://svn.mirrorbrain.org/viewvc/mirrorbrain/trunk/tools/scanner.pl?r1=7911&r2=7945
    was incomplete in so far that the FTP client used a wrong path now when
    cd'ing into a directory (complete URL instead of only the path component).
    This may have worked with some FTP servers, but it definitely didn't work
    with vsftpd. Thanks to Deepak Gupta for raising this issue and providing
    means to analyse it.
  - When using the scanner with ``--enable``, to enable a mirror after
    scanning, it was counter-intuitive that the redirection to the mirror was
    not immediately happening. The mirrorprobe first needs to mark the mirror
    online. The scan tool now does this right away. This issue (`issue 59`_)
    had repeatedly puzzled people.

* :program:`mb edit`:

  - Problems that occurred when copying and pasting data on the editing window
    have been fixed (reported in `issue 30`_).

* :program:`mirrorprobe`:

  - A hard-to-catch exception is now handled. If Python's socket module ran
    into a timeout while reading a chunked response, the exception would not be
    passed correctly to the upper layer, so it could not be caught by its name.
    We now wrap the entire thread into another exception, which would otherwise
    be bad practice, but is probably okay here, since we already catch all
    other exceptions. This should fix `issue 46`_.
  - In case of exceptions we run into, allow logging the affected mirror's name.
  - If an unhandled exception occurs, a note is printed.

* :program:`null-rsync`:

  - Broken links that are replaced by a directory, and point outside the tree,
    are now correctly removed in the destination tree. (A very special case.)
  - Some error messages were improved.



.. _`issue 17`: http://mirrorbrain.org/issues/issue17
.. _`issue 18`: http://mirrorbrain.org/issues/issue18
.. _`issue 30`: http://mirrorbrain.org/issues/issue30
.. _`issue 46`: http://mirrorbrain.org/issues/issue46
.. _`issue 52`: http://mirrorbrain.org/issues/issue52
.. _`issue 59`: http://mirrorbrain.org/issues/issue59

Internal changes:

* :program:`mod_mirrorbrain`:

  - Code was generally cleaned up and logging improved.
  - A hex decoder for efficient handling of binary data from PostgreSQL was added.
  - Old obsolete code has been removed, which was needed before 2009 when
    mod_geoip didn't support continent codes yet. Since then, compiling with
    GeoIP support built-in was still optionally possible, but this old code is
    now removed.
  - The code path has been cleaned up a lot for easier handling of different
    representation, like hashes that are requested.
  - The message which is logged when no hashes where found in the database has
    been enhanced.
  - The obsolete support for generation of plaintext mirror lists
    (application/mirrorlist-txt) has been removed.

* :program:`mb`:

  - Interruptions by Ctrl-C and various other signals are now properly caught.
  - The error classes have been revamped and modernized for Python 2.6.
  - The script mirrordoctor.py has been renamed to mb.py, in order to avoid
    confusion. The tool should now be installed with its own name now, and no
    further symlinking is needed upon installation. 

* :program:`mb makehashes`:

  - Hashes are also stored for files which exists only locally, and not on any
    mirror (and which weren't present in the ``filearr`` table yet, therefore).
    The cleanup mechanism had to be reworked to take this into account.



Documentations improvements:

* The installation docs have been restructured: Now there's a new section
  explaining the :ref:`initial_configuration`, and this part is linked from all
  platform-specific sections as "next step" at their end. This should avoid
  some confusion. Hand in hand with this change, a cleanup of things scattered
  in all places is in progress.

* A few hints about :ref:`tuning_postgresql` were added to the :ref:`tuning`.

* :ref:`initial_configuration_logging_setup` is described in more detail.
 
* Notes about the necessity of :ref:`initial_configuration_file_tree` have been
  added, and alternatives explained.

* Reasons why or why not to use `mod_asn <http://mirrorbrain.org/mod_asn/>`_
  are discussed in :ref:`installing_mod_asn`. 
 
* Installing from Debian packages: There is now a note about expired keys, and
  how to renew them.

* The obsolete MySQL database schema has been removed, which could
  theoretically be useful for people aiming to run only mod_mirrorbrain, but
  not the rest of the framework - but is confusing and may cause people assume
  that MySQL is supported as backend.


Other improvements:

* :program:`rsyncinfo`:

  `This script
  <http://svn.mirrorbrain.org/viewvc/mirrorbrain/trunk/tools/rsyncinfo?view=markup>`_
  is easier to use now. Instead of the arkward syntax it now also takes simple
  rsync URLs. Before::

    rsyncinfo size gd.tuwien.ac.at -m openoffice

  Now::

    rsyncinfo size gd.tuwien.ac.at::openoffice
    rsyncinfo size rsync://gd.tuwien.ac.at/openoffice

* :program:`bdecode`:

  A new tool `bdecode
  <http://svn.mirrorbrain.org/viewvc/mirrorbrain/trunk/tools/bdecode?view=markup>`_
  to parse a Torrent file (or other BEncoded input), and pretty-print it.
  Useful mainly to work on the Torrent generator in mod_mirrorbrain, but also
  to compare the generated torrents with torrents that you get from other
  generators. The tool can take an argument, or read from standard input:: 
    
    bdecode foo.torrent
    curl -s <url> | bdecode


Please read the `2.13.0 upgrade notes`_ before upgrading.


Thanks for all the help!

.. _`2.13.0 upgrade notes`: http://mirrorbrain.org/docs/upgrading/#from-2-12-x-to-2-13-0




Release 2.12.0 (r7957, Feb 10, 2010)
------------------------------------

This release contains several important bug fixes, a new feature,
and documentation fixes.

The new feature is that geographical coordinates of mirrors are stored. This
affects newly created mirrors, as well as mirrors whose metadata is updated
with :program:`mb update -c`. The data are obtained from the GeoIP database, if
available. Note that only the `GeoIP city (lite)`_ database contains this kind of
data. The coordinates aren't used for anything yet, but it's easily possible
now to display mirrors on a map, or to use them to aid mirror selection (which
seems helpful in some cases; see `issue 34`_ for a proposal).

.. _`GeoIP city (lite)`: http://www.maxmind.com/app/geolitecity


For that, :program:`mb update` got a new option ``--coordinates`` to insert (or
update) geographical coordinates in the mirror's database records. The command
can be used to add the data to existing mirrors. Just use ``mb update --coordinates --asn --prefix`` to update all mirror records with the coordinates, as well as refreshing asn and prefix data.


Bug fixes:

* :program:`mb scan`

  - If :program:`rsync` is 3.0.0 or newer, :program:`mb` now uses the
    ``--contimeout`` option in addition to ``--timeout``. This fixes `issue
    12`_, where problems during opening the connection could lead to an
    infinite hang, because that period isn't covered by rsync's ``--timeout``
    option. The additional option to configure this timeout became available
    with rsync 3.0.0.
  - Scanning with FTP authentication has been implemented (URLs in the format
    `ftp://user:pass@hostname/path`).  

* :program:`mb mirrorlist`

  - When generating mirror lists, authentication data (in the form of
    `user:password@`) is now removed from URLs. The assumption is that if URLs
    contain such data, it will almost surely be not the intention to publish them.

* :program:`mod_mirrorbrain`

  - On some platforms, :program:`mod_mirrorbrain` didn't construct proper
    filenames for the metalink hash cache. The bug was reported for Debian
    Lenny, and probably also affected some version of Ubuntu (`issue 35`_). This
    is fixed by using the APR library function :func:`apr_off_t_toa` instead of
    ``%llu`` in the format string fix. Thanks Cory for reporting and tracking
    this down!
  - When Metalinks contained FTP URLs, the URL scheme (``url type`` in the XML)
    was incorrectly set to ``http``. (`issue 23`_). This has been fixed.

* :program:`mb db shell`

  - This new command to spawn a database shell turned out to work only by
    accident -- :func:`os.execlp` was used wrongly (missing its 0th argument).
    This has been correected.

* :program:`mb file ls -u`

  - When using the ``-u`` option with this command to display URLs, broken URLs
    could result if a base URL doesn't end in a slash (`issue 36`_).
    Thanks Vittorio for reporting!

* :program:`mb new` and :program:`mb update`

  - A stupid error in the selection of the best GeoIP database has been fixed.
    A forgotten `break` in the code caused the least preferable database to be
    chosen, of more than one acceptable database file was available.
  - Geographical coordinates are saved to mirror database records.
  - The readability of DNSrr warnings is improved.
  


Since when the metalink hash cache had been reimplemented with release
2.10.0 and 2.10.1, there remained a migration path in :program:`mod_mirrorbrain`
and :program:`metalink-hasher` for reusing the existing hash files. Since this
is several versions away (or 5 months), this migration path has been cleaned
up in both :program:`mod_mirrorbrain` and :program:`metalink-hasher`.

- Backward compatibility and migration support (added around r7794) for old
  filename scheme (``.inode_$INODE``) in the metalink hash cache removed.
- Backward compatibility (added in r7787) for old filename scheme
  (``.metalink-hashes``) in the metalink hash cache removed.

When updating from an installation older than 2.10.1, that is no problem -- it
just means that metalink hashes will be regenerated before they can be used
again.

The documentation was enhanced in the following places:

* A few examples for using cURL for testing have been added.
* The example for creating metalink hashes was wrong. This was fixed, and
  some more details added.
* The usage info of :program:`mb update` was improved.
* The :program:`mb update` command has been documented
  (:ref:`editing_mirrors_network_location`).

.. _`issue 12`: http://mirrorbrain.org/issues/issue12
.. _`issue 23`: http://mirrorbrain.org/issues/issue23
.. _`issue 34`: http://mirrorbrain.org/issues/issue34
.. _`issue 35`: http://mirrorbrain.org/issues/issue35
.. _`issue 36`: http://mirrorbrain.org/issues/issue36


Release 2.11.3 (r7933, Dec 16, 2009)
------------------------------------

This release contains a number of small improvements in the toolchain, plus
small documentation fixes.

* :program:`null-rsync`:
  
  - IO errors returned by rsync are handled now 
  - remote errors from rsync are ignored now, and we let rsync continue with
    dry-run deletions.

* :program:`mb db sizes`:

  - Sizes of tables from `mod_stats`_ are now shown in addition to
    MirrorBrain's own tables.

* :program:`mb db shell`:

  - The script now uses :func:`os.execlp` instead of :func:`os.system` to spawn
    the database commandline interpreter, because the latter doesn't reliably
    pass ``SIGCONT`` to the subprocess when resuming.

* :program:`mb list`:

  - New options ``-H``, ``-F``, ``-R`` to display HTTP/FTP/rsync base URLs have
    been added.

* :program:`mb mirrorlist`:

  - The script now tries harder to not leave temp files -- also in case of a
    crash (which may happen when working with templates).
  - Add a link to our project in the footer.

Changes in the documentation were: 

- The new ``MirrorBrainFallback`` directive is now documented in the example
  :file:`mod_mirrorbrain.conf`.
- The ``-t 20`` option has been removed from the :program:`mirrorprobe` call,
  since that is the default now. The scan cronjob also has been simplified.
- A hint about ulimits has been removed, which turned out to be a band-aid
  for a purely local problem.
- A hint how to load a database dump with :program:`mb db shell` has been
  added.

.. _`mod_stats`: http://mirrorbrain.org/download-statistics/


Release 2.11.2 (r7917, Dec 5, 2009)
-----------------------------------

This release improves scanning via FTP and adds a few small features:

* :program:`mb scan`:

  - When scanning via FTP, filenames containing whitespace would not be
    recognized. The regular expression that parses the FTP directory listing
    has been extended. In addition, a warning is now printed when a line can't
    be parsed. This hopefully fixes `issue 31`_. 
  - when using the FTP protocol for probing for a file or directory, the wrong
    use of a variable let the result always be negative. This affected
    subdirectory scans (using ``mb scan -d path/to/dir``), which would igore
    some mirrors.

* :program:`mb db`:

  - new command for database maintenance tasks: 

    + :program:`mb db sizes` --- shows sizes of all relations
    + :program:`mb db shell` --- conveniently open a shell for the database 
    + :program:`mb db vacuum` --- cleans up dead references (previously: 
      :program:`mb vacuum`, which still can be used for backwards
      compatibility.) 

* :program:`mirrorprobe`:

  - 60 seconds as timeout have always been a bit long. Change the default
    timeout to 20 seconds, which is also the value suggested in the
    documentation.

.. _`issue 31`: http://mirrorbrain.org/issues/issue31


Release 2.11.1 (r7899, Dec 3, 2009)
------------------------------------

This release fixes a regression in :program:`mod_mirrorbrain` that was
introduced with the 2.11.0 release. It affected Debian and Ubuntu, or more
generally all platforms where the APR (Apache Portable Runtime) is version 1.2,
not 1.3. The version detection at compile time was not working. This has been
corrected, fixing `issue 29`_. Thanks to Cory Fields in tracking down this bug!

.. _`issue 29`: http://mirrorbrain.org/issues/issue29


Release 2.11.0 (r7896, Dec 2, 2009)
------------------------------------

A new feature and lots of bug fixes and minor corrections come with this
release. 

It's now possible to configure fallback mirrors, via Apache config, in the
following form::

    MirrorBrainFallback na us ftp://linuxfreedom.com/ultimate/
    MirrorBrainFallback eu de http://www.ultimate-edition.org/~ue/

Those mirrors are used when no reachable mirror is found in the database.
Thus, these mirrors get all those requests that MirrorBrain would normally
deliver itself (you know, the default fallback behaviour).

They are also used in the mirror lists (with priority 1) and metalinks, and
country/region selection is done like for normal mirrors. They are used
blindly, without knowing their file lists.

This actually allows to run a MirrorBrain instance with a pseudo file tree
(cf.  recently added :program:`null-rsync` script.) 

A "degraded mode" that continues to work in case of database complete outages
is easily achievable now, however for now the code path is less robust in
that regard (*if* fallback mirrors are configured. Otherwise, it shouldn't).
This should be fixed later.

This new feature is still its infancy, but ready to be tested. It may be
subject to refinement, based on future discussion.
  
* Other changes in :program:`mod_mirrorbrain` are:

  - The module now automatically makes sure at compile time that its usage of
    the DBD database API fits to the APR (Apache Portable Runtime) version. The
    issue was that the semantics of reading result rows was with APR 1.3. With
    older APR, different semantics need to be used, which hits Debian and
    Ubuntu. This fixes `issue 7`_.

  - The ``MirrorBrainHandleDirectoryIndexLocally`` directive has been removed.
    It was never actually useful, because we never did (and could) redirect to
    directory listings.  For one, a listing might not be available at each URL
    that we might redirect to.  What's more, since the database only stores
    file paths and not directories, we can't actually look up directories.
    Thus, the directive is now removed, and a warning issued where it is still
    found in the config.

  - The default of ``MirrorBrainHandleHEADRequestLocally`` has been changed to
    ``Off``, and it has been made clearer (in the Apache-internal help text)
    what the default is. This change mainly has the effect that the directive
    does *not* need to be given anymore, in most scenarios.
  - The default setting of the ``MirrorBrainMinSize`` directive has been
    documented in its help text.

* The documentation for installation on Debian Lenny was tested and corrected
  where needed. Thanks, TheUni! Minor issues in the Debian packages have been
  improved, to further simplify the installation. Ubuntu benefits from this as
  well.

* :program:`mb`

  - Parse errors in the configuration file are not caught and and reported
    nicely.
  - Special characters occurring in the password are escaped before passing
    them to SQLObject/psycopg2, thus fixing `issue 27`_. A remaining issue is
    that double quotes can't be used; a warning is issued if it's attempted.

* :program:`mb scan`:

  - A warning that appeared since the last release has been removed. It was
    caused by the removal of obsolete code, and purely cosmetic.

* :program:`null-rsync`

  - An ``--exclude`` commandline option has been implemented, to be passed
    through to :program:`rsync`. 
  - Control over the program output can now be exerted by the two new options
    ``--quiet`` and ``--verbose``.
  - Usage info is implemented (``--help`` etc.).
  - Interruptions by :kbd:`Ctrl-C` and similar signals are intercepted now.

* :program:`metalink-hasher`

  - When comparing the modification time of a saved metalink hash with that of a
    source file, the sub(sub-)second portion of the value could be different
    from the value that has just been set by :func:`os.utime`. (Quite
    surprisingly.) So now, we compare only the :func:`int` portion of the
    value. This fixed `issue 24`_.

.. _`issue 7`: http://mirrorbrain.org/issues/issue7
.. _`issue 24`: http://mirrorbrain.org/issues/issue24
.. _`issue 27`: http://mirrorbrain.org/issues/issue27


Release 2.10.3 (r7871, Nov 28, 2009)
------------------------------------

This release adds a new script, which hopefully opens up interesting new use
cases, called :program:`null-rsync`. This is a special rsync wrapper which
creates a local file tree from a mirror, where all files contain only zeroes
instead of real data. The files are created as *sparse files*, so only the
metadata occupies actual space in the filesystem. Modification times and sizes
are fully copied, so that even (native) rsync thinks that the file tree is
identical. 

This script should allow to create a pseudo mirror of arbitrary size (or
several mirrors), in order to host MirrorBrain instances which run under the
precondition that they *always* redirects. (This scenario hasn't tested yet,
but should work.) At any rate, it is a good basis for experimentation.

Then, this release fixes some usability issues in the :program:`mb` tool:

* :program:`mb new`:

  - when creating a new mirror, and detecting that the hostname resolved to
    multiple addresses (round-robin DNS), a warning about this fact was issued.
    Now, (short of documentaion in the manual) a reference to
    http://mirrorbrain.org/archive/mirrorbrain/0042.html is added, where the
    issue has been discussed in depth.
  - A proper error message is now shown if an identifier is chosen that already
    exists.

* :program:`mb mirrorlist` / :program:`mb marker`:

  - The order in which mirrorlist columns are presented is now kept unchanged,
    so it appears as it was entered into the database.
  - The sort order of mirrorlist entries has been improved. Instead of the
    priority, the mirror operator name is now given precendence in order, which
    results in a mirror list that actually *looks* sorted.


Release 2.10.2 (r7853, Nov 9, 2009)
-----------------------------------

Some non-code changes that should be mentioned:

* The documentation was updated in various places. Notably, there are now
  instructions for :ref:`installation_ubuntu_debian`, which David Farning
  deserves credits for.

* Ubuntu (and Debian) packages have been created. The Ubuntu packages have been
  tested successfully. (See download page.)

* A bug tracking system has been set up: http://mirrorbrain.org/issues/

In the code, the following bugs were fixed:

* The :program:`mirrorprobe` could crash when the sender domain of a
  configured mail log handler wasn't resolvable (`issue #9`_). This has been
  fixed.

* When scanning a subdirectory, the mirror scanner (:program:`mb scan`) could
  accidentally delete files from the database outside of that directory. This
  was caused by lack of terminatation (with a slash) of the path expression
  that is used to grab the list of known files before the scan. Herewith
  fixing `issue #19`_.

* A misleading error message in the :program:`mb` tool was improved, which
  was issued when encountering config file with missing sections.


.. _`issue #9`: http://mirrorbrain.org/issues/issue9
.. _`issue #19`: http://mirrorbrain.org/issues/issue19

Release 2.10.1 (r7798, Sep 9, 2009)
-----------------------------------

* The implementation of the hash cache created by the
  :program:`metalink-hasher` tool has been revised again. The reason is that
  some filesystems (at least the VirtualBox Shared Folder) don't implement
  stable inode numbers. Instead of the inode number, now the file size (plus
  filename and modification time) is used to identify file hashes. (These are
  the same criteria that rsync uses, by the way.)

  Existing hashes are migrated, so that the files don't need to be hashed again
  (which could potentially be time-consuming).
  
  The modification time of files is now copied to the hash file, so it is
  available for comparison when checking if a hash file is up to date.

  :program:`mod_mirrorbrain` has been adapted for the new cache scheme.
  Also, it is now required that the modification time of the hash file matches
  the modification time of the file. (For backwards compability, the module
  still also checks for files matching the old scheme.)
  
  To ease the migration, and since it doesn't matter otherwise, non-existance
  of files to be unlinked is ignored now. This occurs for instance in the above
  mentioned migration scenario, where the hash files are renamed to a different
  name.
  

* New features in the :program:`metalink-hasher` tool:

  - Per-directory locking was implemented: directories where already a job is
    running will be skipped. This allows for hassle-free parallel runs of more
    than one job. 
  
    Note that simultaneous spawning of the script still needs to be controlled,
    to avoid consuming too much I/O or CPU bandwidth for a machine. 

  - Ctrl-C key presses and common interrupting signals are now handled
    properly.



Release 2.10.0 (r7789, Sep 4, 2009)
-----------------------------------

* The cache of metalink hashes, as created by the :program:`metalink-hasher`,
  was changed to more reliably detect changes in the origin files. So far, the
  file modification time was the criterion to invalidate cached hashes. When
  files were replaced with *older* versions (version with smaller mtime), this
  wasn't detected, and a cached hash would not be correctly invalidated.
  https://bugzilla.novell.com/536495 reports this of being an issue.
  
  To fix this, the cache now also uses the file inode as criterion.

  :program:`mod_mirrorbrain` was updated to use the new inode-wise metalink
  hashes. At the same time, it still knows how to use the previous scheme as
  fallback. If the new-style hash isn't found, it looks for the old-style hash
  file.
  
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

  - :program:`mb scan` and :program:`mb file ls --probe`:

    - the lookup whether the :mod:`multiprocessing` or :mod:`processing` module
      exist was fixed: it could print a false warning that none of them was
      installed.

* The :program:`mirrorprobe` program no longer logs to the console (stderr).
  This allows for running the script without redirection its output to
  :file:`/dev/null` — which could mean swallowing important errors in the end.

  A scenario was documented where the mirrorprobe could fail on machines with
  little memory and many mirrors to check. The fix is to properly set ulimits
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
* :program:`mb dirs`: new subcommand for showing directories that the database contains,
  useful to tune scan exclude patterns.
* :program:`mb export`: implement a new output format, named ``vcs``. Can be used to commit
  changes to a subversion repository and get change notifications from it. See 
  http://mirrorbrain.org/archive/mirrorbrain-commits/0152.html
* Partial deletions (for subdir scans) have been implemented.
* :program:`mb list` accept ``--country`` ``--region`` ``--prefix`` ``--as``
  ``--prio`` options to influence which details are output by it.
* :program:`mb file`: support for probing files, with optional md5 hash check of the
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

