
Installing mod_asn
------------------

mod_asn is optional for MirrorBrain. MirrorBrain runs fine without it. If you
don't need mirror selection based on network prefix or autonomous system, you
don't need to install mod_asn.

.. note::
   There was a bug in the :program:`mb` tool that it depended on the existance on
   a database table named ``pfx2asn`` which is created when mod_asn is installed. The
   bug was fixed in the 2.9.0 release.

To install mod_asn, refer to the `its documentation`__.

__ /mod_asn/docs/


