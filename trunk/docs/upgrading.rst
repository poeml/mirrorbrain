.. _upgrading:

Upgrading
=========

Upgrading PostgreSQL
--------------------

When upgrading PostgreSQL, it is important to look at the version number difference. 
If the third digit changes, no special procedure is needed (except when the
release notes explicitely hint about it).

When the first or second digit change, then a dump-and-reload cycle is usually
needed. 

For instance, when upgrading from 8.3.5 to 8.3.7 nothing needs to be done. When
upgrading from 8.3.7 to 8.4, you need to dump and reload.

You might want to follow the instructions that your vendor provides. If your
vendor doesn't provide an upgrade procedure, be warned that the database needs
to be dumped before upgrading PostgreSQL.

See ``pg_dumpall(1)`` for how to dump and reload the complete database.

.. warning:: 
   If you use mod_asn, there is one more caveat. If your vendor's upgrade
   procedure automatically saves the previous PostgreSQL binaries in case they
   are needed later, the procedure might not take into account that the
   :file:`ip4r.so` shared object might need to be saved as well.  Hence, you
   might be unable to start the old binaries, when the ip4r shared object has
   been upgraded already.

Hence, it is recommended that you do a complete dump of the databases before
upgrading, and load that after upgrading.


.. note:: 
   When upgrading to 8.4, ``ident sameuser`` is no longer a valid value in
   :file:`pg_hba.conf`. Replace it with ``ident``.
