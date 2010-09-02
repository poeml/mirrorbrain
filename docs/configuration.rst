
Configuring MirrorBrain
=======================

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
zsync release (0.6.1).

If mirrors are available for a file, MirrorBrain adds them into the zsync as
URLs where missing data can be downloaded. zsync-0.6.1 requires real mirrors as
URLs, not one URL that redirects to mirrors. It is noteworthy in this context
that zsync client (as of 0.6.1) tries the provided URLs in random order. Thus,
the listed URLs are restricted to the ones that are closest. Thereby, the zsync
client will use nearby mirrors to download data from.

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

Hashes are hex-encoded, because Base32 encoding would be awkward to add and
there seems to be a transition to hex encoding.

The ``urn:sha1`` scheme is currently also not supported, because it is required
to be Base32-encoded. Base32 encoding could be added in the future, of course.
Contributions welcome!




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
   random request distribution via mod_rewrite.


.. _`mod_setenvif`: http://httpd.apache.org/docs/2.2/mod/mod_setenvif.html
