
Further configuring MirrorBrain
===============================

This chapter contains notes about important extra things that you may want to
configure, and also some more exotic configurations.

For basic configuration, please refer to the installation section
(:ref:`initial_configuration`).


.. _configuring_torrent_generation:

Generating Torrents
-------------------

From the hashes generated with :program:`mb makehashes`, MirrorBrain can
generate not only Metalinks, but also Torrents. The required chunked hashes are
the same. 

The generation is triggered by appending ``.torrent`` to an URL.  

The torrents have the following properties:

- They include (a selection of closest) mirror URLs, for web seeding. They
  are included both in a ``url-list`` field as well as a ``sources`` field.
  Only a selection of mirrors is included, because there is a chance that some
  clients will just pick a random mirror from the list. Thus, only the mirrors
  that are closest to a client are included.

- Some clients (at least the original BitTorrent client) can have difficulties
  to grok modern fields that they do not know. (They might error out and claim
  invalid bencoding, for instance.) Putting some keys behind, not before, the
  info dictionary seems to help. Therefore, ``url-list`` and ``sources`` are
  included at the end.

- A DHT "nodes" list is included in Torrents (see `issue 49
  <http://mirrorbrain.org/issues/issue49>`_). It needs to be configured with
  the ``MirrorBrainDHTNode`` Apache configuration directive.

- They include the MD5, SHA1 and SHA256 hash into the info dictionary.

- If a torrent is requested, but data needed for it is missing, a 404 is returned.

- Multiple trackers can be included. See below.

- The BitTorrent Info Hash (``btih``), which uniquely identifies a torrent (by
  an SHA1 hash over the bencoded ``info`` dict) is shown in the mirror list
  (metadata view). The info hash can also be requested separately from the
  server by appending ``.btih`` to an URL.

- Hashes that require Base32 encoding are currently not included. Base32
  encoding could be added, of course, if somebody is willing to do this.



Performance considerations
~~~~~~~~~~~~~~~~~~~~~~~~~~

The piece length is configurable by a parameter in
:file:`/etc/mirrorbrain.conf`. The default is ``chunk_size = 262144`` bytes
(see :mod:`mb.hashes`). The size has a direct relation to the space that the
hashes occupy in the database. To find out how much it is, the :program:`mb db
sizes` command can be helpful. Note the size of the ``hash`` table.

If space is of utter concern, generation of chunked hashes by can be switched off
with ``chunked_hashes = 0`` in :file:`/etc/mirrorbrain.conf`. This effectively
disables generation of torrents. (Metalinks will still be generated, they'll
just not contain piece-wise hashes.)

Parameters need to go into the mb instance section, not into the ``[general]``
section.


Configuration
~~~~~~~~~~~~~

There are the following Apache configuration directives to configure the torrents.
Both go into a virtualhost context, and not inside a directory context.

- ``MirrorBrainTorrentTrackerURL``
 
  Defines the URL a BitTorrent Tracker to be included in Torrents and in Magnet
  links. The directive can be repeated to specify multiple URLs. Here's an
  example::

      MirrorBrainTorrentTrackerURL "http://bt.mirrorbrain.org:8091/announce"
      MirrorBrainTorrentTrackerURL "udp://bt.mirrorbrain.org:8091/announce"

  The first URL is put into the ``announce`` key. All URLs will be listed in
  the ``announce-list`` key.


- ``MirrorBrainDHTNode`` 
  
  Defines a DHT node to be included in Torrents links. The directive can be
  repeated to specify multiple nodes, and takes two arguments (hostname, port).
  Example::

      MirrorBrainDHTNode router.bittorrent.com 6881
      MirrorBrainDHTNode router.utorrent.com 6881


Registering with a tracker
~~~~~~~~~~~~~~~~~~~~~~~~~~

Most trackers require registration of the torrents, or they refuse to deal with
them. Unless you use a tracker that allows access to unregistered torrents, you
will have to take care of the registration.

At the moment, this means downloading the torrents from MirrorBrain and doing
this somehow. Or using an open tracker.

In the future, it is planned that MirrorBrain writes copies of the torrents to
the file system, so they can easily be rsynced to somewhere else, where the
registering process runs.


.. _configuring_zsync_generation:

Configuring zsync support
-------------------------

The `zsync distribution method <http://zsync.moria.org.uk/>`_ resembles rsync
over HTTP, so to speak, and is a very bandwidth-efficient way to synchronize
changed individual files; at the same time, it is very scalable, because the
main work is not done at the server (as with rsync), but distributed to the
clients. 

Normally, zsync metadata needs to be generated manually and saved in form of
.zsync files next to the real files.

MirrorBrain however has support for generating the required checksums, and
can then serve the zsync files generated on-the-fly. The generation is
triggered by appending ``.zsync`` to an URL.  

The supported method reflects the "simpler" zsync variant, which doesn't look
into compressed content. It is compatible to, and was tested with, the current
zsync release (0.6.2). 0.6.1 worked as well.

If mirrors are available for a file, MirrorBrain adds them into the zsync as
URLs where missing data can be downloaded. zsync (0.6.1) requires real mirrors as
URLs, not one URL that redirects to mirrors. It is noteworthy in this context
that zsync client (as of 0.6.1) tries the provided URLs in random order. Thus,
the sent URLs are restricted to the ones that are closest. Thereby, the zsync
client will use more nearby mirrors to download data from.

If no mirrors are available, a valid zsync is still generated. The content will
then be delivered directly by MirrorBrain.

.. note::
   This feature is off by default, because Apache can allocate large amounts of
   memory when retrieving very large rows from database (and keeps it). This
   may or may not affect you; and it may be worked around in the future.
   (The amount of memory that Apache allocates is about twice the size of the
   largest zsync, so in the end it depends on the file sizes.)

To activate zsync support, you need to switch on the generation of the special
zsync checksums. That is done like shown below in the MirrorBrain instance
section into :file:`/etc/mirrorbrain.conf`::

        [general]
        # not here!

        [your mb instance]
        dbuser = ...
        ...
        zsync_hashes = 1


This will cause :program:`mb makehashes` generate the zsync checksums and store
them into the database. See :ref:`creating_hashes` for more info on this tool.
This tool needs to be run periodically, or after known changes in the file
tree, to update the checksums.

.. note::
   For the fastest possible checksumming, the algorithm is implemented in C
   (zsync's own "rsum" checksum) and integrated via a C Python extension.

The checksums occupy space in the database. To find out how much it is, the
:program:`mb db sizes` command can be helpful. Note the size of the ``hash``
table.



.. _magnet_links:

Magnet links
------------

The Magnet URI scheme allows to reference a file for download via P2P networks.
See `Wikipedia <http://en.wikipedia.org/wiki/Magnet_URI_scheme>`_ and the
`project website <http://magnet-uri.sourceforge.net/>`_.

Magnet links are automatically included in Metalinks (v3 Metalinks as well as
IETF Metalinks). They also appear in the mirror list.

Magnet links can contain a BitTorrent tracker URL. MirrorBrain includes tracker
URLs configured via the Apache ``MirrorBrainTrackerURL`` directive into magnet
links. This means that multiple trackers can be listed. Configuring tracker
URLs is explained in the :ref:`configuring_torrent_generation` section.

A magnet link can be requested from MirrorBrain simply by appending ``.magnet``
or ``?magnet`` to an URL.

Implementation notes:

- Hashes are hex-encoded, because Base32 encoding would be awkward to add and
  there seems to be a transition to hex encoding.
- The ``urn:sha1`` scheme is currently also not supported, because it is
  required to be Base32-encoded. Base32 encoding could be added in the future,
  of course.  Contributions welcome!


.. _yum_style_lists:

Serving Yum-style mirror lists
------------------------------

Yum has a way to request a plain-text list of mirrors from a server. This is
useful because it can then has a number of mirrors available as fallbacks, or
can test which one works fastest for it. Traditionally, Yum is configured with
a URL containing passing a few query arguments to select the appropriate
repository. For example, the communication with the server could look like
this::

   request: 
     http://centos.mirrorbrain.org/?release=5.5&arch=x86_64&repo=os
   reply from server:
     http://ftp.uni-bayreuth.de/linux/CentOS/5.5/os/x86_64/
     http://ftp.hosteurope.de/mirror/centos.org/5.5/os/x86_64/
     http://mirror.sov.uk.goscomb.net/centos/5.5/os/x86_64/


MirrorBrain has support to answer these queries in a meaningful way. The
returned list of mirrors will be sorted by suitability for the client -- with
the full-blown selection algorithm being applied. 10 mirrors will be returned
(that could be made configurable, if need be).


The Apache config directive ``MirrorBrainYumDir`` is used to map the various
possible query arguments to actual directories. Below these directories,
MirrorBrain checks if a certain file present. Only mirrors that list that
marker file will turn up in the mirror list. 

The syntax of the directive is::

    MirrorBrainYumDir <arg>=<regexp> [<arg>=<regexp> ...] <basedir> <mandatory_file>


Here, the meaning of the arguments is:

.. describe:: arg

   One ore more keys that the client uses in the query. The order in these are
   given does not matter.


.. describe:: regexp

   This is a regular expression against the values are matched which Yum sends.
   You are free to use a simple string here, like ``updates``, or a regular
   expression that catches several things.
   
   These regular expressions are forced to be anchored to start and end, for
   security reasons.

   When you use strings like ``10.0``, remember that the dot is a magic in
   regular expressions (matching any character), so you should escape it with a
   backslash (see examples below).

   See the next section for a discussion of security implications.

.. describe:: subdir

   This is the directory that corresponds to the specified query arguments. Its
   path is given relative to the top of the file tree. At the same time, this
   is the path on the mirror servers, relative to the base URL of their mirror.

   Since there can be a need for many different, but similar mappings, there is a way to
   automatically insert values from the query into this path. Any 
   ``$1`` to ``$9`` specified in the ``subdir`` string will be replaced with
   the respective value. 
   
   For instance, ``$2`` is replaced with the *second* ``arg`` value defined
   here. (*Not* with the second argument that yum passes in. That would make no
   sense -- since the order in which it places the values is not specified.) 

   It is not necessary to put ``()`` braces around the regexps, to declare them as a
   "match group". *Every* regexp is a match group.


.. describe:: mandatory_file

   This is a file that needs to exist in the specified subdirectory on the
   mirrors. Again, its path is given relative. 

 
Here's an example with two similar mappings::

    MirrorBrainYumDir release=5\.5 repo=os arch=i586 5.5/os/i386 repodata/repomd.xml
    MirrorBrainYumDir release=4\.8 repo=os arch=i586 4.8/os/i386 repodata/repomd.xml

If the client requests ``?release=4.8&arch=i586&repo=os``, the second rule will
match. MirrorBrain will create a list of all mirrors that are known to have the
file ``4.8/os/i386/repodata/repomd.xml``, and send the 10 best of these mirrors to
the client. The mirror URLs will have the ``subdir`` appended as appropriate.

Here is a more complex (but not contrived) example which demonstrates how a
multitude of cases can be handled with a single rule::

    MirrorBrainYumDir release=(4|4\.8|5|5\.5) repo=(os|updates|extra) arch=i586 $1/$2/i386 repodata/repomd.xml

If the client sends ``?repo=extra&release=5``, the directory becomes
``5/extra/i386``, and so on.

Things you should note:

- Symlinks are automatically handled. Remember that symlinks on mirrors are
  invisible when scanning is done only via HTTP. To avoid having multiple trees
  in your database, you should make sure that only "real" directories are
  scanned via ``scan_top_include`` in :file:`/etc/mirrorbrain.conf`.
  MirrorBrain canonicalizes all paths in the local filesystem before doing a
  lookup in the database. Therefore, it doesn't matter if the query arguments
  are in fact resolving to a symlink to a file tree.

- If no mirror is found in the database, any mirror configured via
  ``MirrorBrainFallback`` is considered. If none of the latter is configured,
  MirrorBrain at least returns its own URL, which, after all, will give the
  client a working download, so it should be better than nothing.

- The client needs to specify all arguments defined in a MirrorBrainYumDir, and
  all must match.

Security considerations are discussed in the following section.

Security notes
~~~~~~~~~~~~~~

- Because MirrorBrain uses strings that are passed in from clients, which could
  potentially be malicious, these are handled with care. 
  
  The normal resource limits of request processing in Apache apply. There is no
  special sanitizing for '/../' elements in the path. 

  Such arguments are accepted if the regular expression leaves room for that --
  for instance, if it contains wildcards as ``.*``.
  
  In any case, the resulting path is canonicalized in the local file system. It
  is assumed that this cannot have bad effects. The most that an attacker can
  achieve is that a path is canonicalized to an existing file on the server.
  For Apache, this will mean that it (debug-) logs something like::
  
    Error canonicalizing filename '/srv/nullmirrors/centos/5/os/x86_64/../../../../../../etc//repodata/repomd.xml'

  Canonicalization still fails because the file checked is always the one
  specified by the admin. Even if the canonicalization would accidentally work,
  no information about the file, or even about the success or failure, would be
  returned to the client. 
  
  Any error in canonicalization will stop processing of the request. Success
  (let's assume it is possible) will result in the path being used in a
  database SQL query, asking for mirrors that have the file, which is highly
  unlikely. The SQL argument is passed to the database via a prepared statement
  with bound parameters, so there is no chance for SQL injection attacks.

  (And all Apache logging undergoes escaping, excluding viewing logs etc. as
  attack vector.)
  
  Still, it seems prudent to recommend to downright avoid the issue by using
  more specific regular expressions that accept only what you want.



.. _styling_details_pages:

Styling the mirrorlist / details pages
--------------------------------------

MirrorBrain generates per-file pages with all known metadata and a list of
mirrors. This is triggered by appending ``.mirrorlist`` or ``?mirrorlist`` to a
request. The pages are delivered with character set UTF-8 in the Content-type
header.

The content of these pages are wrapped into a XHTML/HTML ``DIV`` container with
``id="mirrorbrain-details"``. This gives a means for styling in conjunction
with a stylesheet linked in via the ``MirrorBrainMirrorlistStyleSheet``
Apache config directive. ``MirrorBrainMirrorlistStyleSheet`` goes into
virtualhost context and takes an URL, which can be relative or absolute.
Either of the following would work::

    MirrorBrainMirrorlistStyleSheet "http://www.poeml.de/~poeml/mirrorbrain.css"
    MirrorBrainMirrorlistStyleSheet "/mirrorbrain.css"

Further, arbitrary design can be achieved by specifying the XHTML/HTML
header and footer which are placed around the page body instead of the built-in
XHTML. This is configured with the following two Apache configuration
directives, which go into virtualhost context::

    # Absolute path to header to be included at the top
    MirrorBrainMirrorlistHeader /srv/www/htdocs/mb-header.html

    # Absolute path to footer to be appended
    MirrorBrainMirrorlistFooter /srv/www/htdocs/mb-footer.html



.. _configuring_url_signatures:

Configuring URL signatures
--------------------------

Signing URLs can be a way to protect content from unauthorized access. How does
this work?

The redirector can generate redirection URLs that contain a signature with
temporary validity. The signature can be checked on the mirror servers to
restrict access to authenticated clients. This means that authentication and
authorization of clients can be validated centrally (by the redirector), and
the content on all mirrors protected. 

To configure URL signing, the server needs to be configured with a secret key::

    MirrorBrainRedirectStampKey my_key

In this example, ``my_key`` is an arbitrary string known only to the server
administrators (including the mirror servers).

The directive may be used server-wide, or applied to certain directories.

It makes sense to combine this with setting up authentication to restrict
access. When clients access the server *and* are authorized to access the file,
and the server decides to redirect the request, the result is as demonstrated
in the following example::

     # curl -sI http://192.168.0.117/extended/3.2.0rc5/OOo_3.2.0rc5_20100203_LinuxIntel_install_ar.tar.gz
    HTTP/1.1 302 Found
    [...]
    http://ftp.udc.es/OpenOffice/extended/3.2.0rc5/OOo_3.2.0rc5_20100203_LinuxIntel_install_ar.tar.gz?time=1288879347&stamp=e215bb55bbea2c133145330f9e061f5b

Note the two arguments that are appended to the URL in the Location header.

The mirrors which receives these requests need to check two things:

1) Check if the "ticket" is a valid one, by hashing the timestamp and
   the shared secret -- here in shell code::

        # echo -n '1288879347 my_key' | md5sum
       e215bb55bbea2c133145330f9e061f5b  -  

   That hash is the same as came with the URL, and thus the ticket
   is valid (or was valid in the past).


2) Check if the "ticket" is still fresh enough, simply by comparing to
   current time. Shell example::

        # echo $(( $(date +'%s') - 1288879347 ))
       380

   While I was typing, the ticked became 380 seconds old (because I'm
   slow :-), which makes it outdated, and the mirror would reject it,
   based on the policy that it must not be older than e.g. 30 seconds.

These two checks to be done on the mirrors can easily be destilled in a
script consisting of only a few lines.

The fact that MD5 collisions can be found nowadays should be pretty
irrelevant considering the short-lived-ness of the tickets. That's one
reason why the mirrors should check for the age. The other reason is
because it allows using a one-way, non-revertible hash, instead of a
symmetric encryption which would be less trivial to implement (md5 is
available anywhere, while symmetric ciphers are not).



Using mod_mirrorbrain without GeoIP
-----------------------------------

mod_mirrorbrain can be used without GeoIP. This could happen in (at least) two
ways:

1) Let's assume that GeoIP *cannot* be used: this would be the case if the
   traffic to be redirected is in an intranet.

   Country information that mod_mirrorbrain uses to select mirrors can be faked
   with the standard Apache module `mod_setenvif`_ as in the following
   example::

        SetEnvIf Remote_Addr ^10\.10\.*      GEOIP_COUNTRY_CODE=us
        SetEnvIf Remote_Addr ^10\.10\.*      GEOIP_CONTINENT_CODE=na
        SetEnvIf Remote_Addr ^192\.168\.2\.* GEOIP_COUNTRY_CODE=de
        SetEnvIf Remote_Addr ^192\.168\.2\.* GEOIP_CONTINENT_CODE=eu

   The ``SetEnvIf`` directive sets two variables for each client per its
   network address. Thus, you can configure your mirrors in the database to
   reflect those countries. You could make up pseudo country codes, if needed.
   
   Based on this information, mod_mirrorbrain will chose an appropriate
   server.

2) Let's assume that a simple round-robin distribution of requests is
   sufficient. This would be the case if the clients are all located in the
   same network or country. There is nothing to configure for this.

   In such a scenario, mod_mirrorbrain will farm out the requests to all the
   available mirrors by random. It will still do this according to the
   preference of each mirror, and according to availability of the requested
   files on each mirror. Mirror selection criteria as the online status of each
   mirror will still apply. 
   
   Thus, this solution is more powerful than simple DNS-based round robin, or
   random request distribution via mod_rewrite. (Of course, contrary to those
   other solutions, it requires tracking the mirrors' status and contents.)


.. _`mod_setenvif`: http://httpd.apache.org/docs/2.2/mod/mod_setenvif.html
