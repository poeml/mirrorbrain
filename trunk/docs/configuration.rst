
Configuring MirrorBrain
=======================

To be written.










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
   sufficient. This would be the case if the clients are all located in the same network or country. 

   In such a scenario, mod_mirrorbrain will farm out the requests to all the
   available mirrors by random. It will still do this according to the
   preference of each mirror, and according to availability of the requested
   files on each mirror. Mirror selection criteria as the online status of each
   mirror will still apply. 
   
   Thus, this solution is more powerful than simple DNS-based round robin, or
   random request distribution via mod_rewrite.


.. _`mod_setenvif`: http://httpd.apache.org/docs/2.2/mod/mod_setenvif.html
