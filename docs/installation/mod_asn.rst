
.. _installing_mod_asn:

Installing mod_asn
------------------

`mod_asn <http://mirrorbrain.org/mod_asn/>`_ is an optional extension for
MirrorBrain. It is not required. 

mod_asn allows a refined mirror selection upon certain network characteristics.

*Without* mod_asn, MirrorBrain selects an appropriate mirror by geolocation
through the GeoIP database. Thus, the client gets his download from a mirror
within the same country, or if that isn't possible from a mirror within the
same continent at least. That's often all that you need.

*With* mod_asn, MirrorBrain additionally looks at the network prefix that the
client's IP address is contained in, and the autonomous system (AS) that it
belongs to, and (if possible) matches that to the networks that the mirrors
live in.  This can be useful in the following scenarios:

- You have a lot of mirrors - so many, that there is a chance that a client
  might have a mirror on his own network. mod_asn will find it by matching the
  AS and network prefix.  For instance, if there's a large ISP who run a
  mirror, and they probably have a significant number of downloaders among
  their customers, it is generally desirable to send them to their ISPs mirror,
  instead of somewhere else.

  On the contrary, if such "network locality" does not exist, there is no
  reason to look up the network prefix or autonomous system and match them to
  the mirrors. For instance, say you have exactly one US mirror, one in India,
  and one in Germany; then GeoIP is good enough to assign the clients to their
  closest mirror simply by looking at the client's country.

- You have a certain mirror that has a lot of users on the same network, and
  you want to make sure that those clients all get sent to that mirror, and not
  just to *any* mirror in the same country (what GeoIP alone would do).

- You have a certain mirror that doesn't want to get requests from any network
  other than its own. Only local traffic is wanted, by policy. MirrorBrain
  knows which clients are "local", and you set a flag in the mirror's config
  (``prefixOnly`` or ``asOnly``) which tells MirrorBrain that the mirror should
  not get any other traffic. The mirrors will be happy to know about this
  possibility.


If you don't need mirror selection based on
network prefix or autonomous system, you don't need to install mod_asn.

To install mod_asn, refer to the `its documentation`__.

__ /mod_asn/docs/


