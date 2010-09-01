
Configuring MirrorBrain
=======================

This chapter contains notes about important extra things that you may want to
configure, and also some more exotic configurations.

For basic configuration, please refer to the installation section.



.. Generating Torrents
.. -------------------



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
can serve the zsync files generated on-the-fly. The files reflect the "simpler"
zsync variant, which doesn't look into compressed content. It is compatible to,
and was tested with, the current zsync release (0.6.1).

.. note::
   This feature is off by default, because Apache can allocate large amounts of
   memory for large rows from database. This may or may not affect you; and it
   may be worked around in the future.

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

If mirrors are available for a file, MirrorBrain adds them into the zsync as
URLs where missing data can be downloaded. zsync-0.6.1 requires real mirrors as
URLs, not one URL that redirects to mirrors. It is noteworthy in this context
that zsync client (as of 0.6.1) tries the provided URLs in random order. Thus,
the listed URLs are restricted to the ones that are closest. Thereby, the zsync
client will use nearby mirrors to download data from.

If no mirrors are available, a valid zsync is still generated. The content will
then be delivered directly by MirrorBrain.




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
