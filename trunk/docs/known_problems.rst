Known problems
==============

Database reconnections issues
-----------------------------

If the database goes away (or is restarted), it is not clear at the moment if
the pgsql database adapter used by Apache's DBD library reconnects cleanly.
Sometimes it seems that it doesn't reconnect, and a graceful restart of Apache
is needed. This needs further inspection.


Passwords containing spaces
-----------------------------

Passwords containing spaces in :file:`/etc/mirrorbrain.conf` are known not to work.
