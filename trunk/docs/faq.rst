.. _faq:

FAQ - Frequently Asked Questions
------------------------------------------

Is it required that all mirror servers have the same data set of files?
=============================================================================

No. On the contrary, this is one of the things where MirrorBrain shines,
in that it doesn't matter at all what the mirrors chose to mirror. In
the extreme, a mirror could have just a single file; it doesn't matter.


Is MirrorBrain suitable only for Linux distributions?
=============================================================================

No, it is designed explicitely for normal web browsers and other download clients, for normal
files to be downloaded. It does not require special software, nor
does it require users to have any Linux experience. Generic applicability and
modularity were among the primary design goals from the beginning.
Another explicit goal was to not design for a specific system or
specific software, or particular file sizes (large or small).

Having said that, MirrorBrain provides useful metadata for more sophisticated download clients.


Is only HTTP supported as protocol?
=============================================================================

No — FTP mirrors are fully supported, in addition to HTTP. Furthermore,
BitTorrent and Metalinks can be used.

To scan mirrors for their content, rsync is used. It is the most
efficient method for that purpose. However, if rsync isn't available on
a mirror, FTP and HTTP can be used as fallback.


Your fundamental design looks like that everything has to be done in a single machine. Right?
=============================================================================================

No, it only means that clients are sent to machines under control of
the owner of the distribution and from there to mirrors. If it is a
single machine or many does not matter. For instance, you could run
different MirrorBrain servers on different continents, and send clients
to the nearest server via GeoDNS.

Do you have plans to support MySQL in addition to PostgreSQL?
==================================================================================

No way, I started with MySQL initially, and that was all good, but then
I needed more compact storage for the vast openSUSE file tree. I could
reduce database size to 30% by migrating to PostgreSQLs more flexible
data types. Speed was increased by factor 5. See `here`__ for details.

But what's more, I later started to use indexed network
addresses (required for autonomous system lookups, in order to match
clients to networks). There is no performant way to do this in MySQL,
since it requires a Patricia Trie as index.  Only PostgreSQL offers a
(third party) data type that can do this, called `ip4r`__. See `mod_asn`__.

By the way, so-called stored procedures are a very nice thing that PostgreSQL
brings along. It makes programming so much easier if the database can be
equipped with high-level procedures on board, that don't need to be sent to
the database, don't need to be parsed over and over. Very elegant to use. I
never looked back.

__ http://mirrorbrain.org/news/27-release-smaller-and-faster-database/
__ http://pgfoundry.org/projects/ip4r/
__ http://mirrorbrain.org/mod_asn/


How can requests/traffic be logged?
=============================================================================

With MirrorBrain, Apache can log additional details like client
location and the chosen mirror. Since MirrorBrain is designed to see all
requests (instead of having clients go directly to mirrors, and not
seeing them ever again), rich logging is possible.

This design also allows to collect download statistics easily, something
that most large projects want. Please refer to the `concept for download
statistics`__ for more on this.

__ /download-statistics/


How much traffic can MirrorBrain handle?
=============================================================================

The openSUSE project has been using MirrorBrain since its inception, and
has pushed at least 250 GBit/s to users (probably more, that's a
hard number measured that doesn't comprise all used mirrors). This has
been served from a really old two-way box getting 20.000.000 to
40.000.000 hits a day, i.e. 250-450 per second, with a load average of
1 or less. The traffic has been distributed to more than 150 active
mirrors (the mirrors obviously being the ones doing the hard work).

Does a copy of the mirrored content have to be kept locally?
=============================================================================

Yes, and no.

Normally, yes. This is by design: it allows for correct handling of If-modified-since requests, you can always deliver files directly, you can always look at the master server(s) what's there (conveniently), and you can exclude certain files from being redirected at all (e.g. signatures, or quickly changing metadata).

There are some more subtle implications regarding the local file tree, for example that Apache can check for the existance of a requested file very quickly, while it would require a database hit otherwise. For tiny files, the database lookup can be saved as well; delivering a file 2048 bytes in size is faster than doing a database lookup and returning a redirect which would be about the same size anyway (and it saves the clients an additional roundtrip to another place).

Of course, each setup is different and all of the above might not matter under given circumstances. As an alternative, there is a way to set up the system without a full local copy of the file tree: the local file tree can be substituted with a dummy file tree. This still needs to be updated, but at least it doesn't take significant space, and guarantees correct handling of If-modified-since requests. You will find details `here <http://mirrorbrain.org/docs/installation/initial_config/#creating-a-file-tree>`_.

.. See here and here.
.. http://mirrorbrain.org/archive/mirrorbrain/0045.html
.. http://mirrorbrain.org/archive/mirrorbrain/0050.html


.. vim: ft=rst:ai:
