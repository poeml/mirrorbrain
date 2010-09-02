.. _tuning:

Tuning guide
============

The following sections describe how to tune PostgreSQL and Apache for good
performance.

Depending on the size of your install, this can be mandatory.


.. _tuning_apache:

Tuning Apache
-------------

MPM
^^^

In general, it makes sense to use a threaded `MPM`_ for Apache. Prefork is less
suitable, because the database connection pool would not be shared across the
worker children. With prefork, each process would open its own connection to
the database. For small installations, this might not matter. However, for a
busy download server, the threaded `event MPM`_ or `worker MPM`_ are better choices.

The following would be a configuration for the event MPM which serves up to 960
connections in parallel, using 64 threads per process. (Values for threads per
process that scale best on Linux are 32 or 64.)::

    # event MPM
    <IfModule event.c>
        ServerLimit           15
        MaxClients           960
    
        StartServers           2
    
        ThreadsPerChild       64
        ThreadLimit           64
    
        MinSpareThreads       32
        # must be >= (MinSpareThreads + ThreadsPerChild)
        MaxSpareThreads      112
    
        # at 200 r/s, 20000 r results in a process lifetime of 2 minutes
        MaxRequestsPerChild 20000
    </IfModule>

Refer to the `Apache MPM documentation`_ for details.


.. _`MPM`: http://httpd.apache.org/docs/2.2/mpm.html
.. _`Apache MPM documentation`: http://httpd.apache.org/docs/2.2/mpm.html
.. _`event MPM`: http://httpd.apache.org/docs/2.2/mod/event.html
.. _`worker MPM`: http://httpd.apache.org/docs/2.2/mod/worker.html


DBD connection pool
^^^^^^^^^^^^^^^^^^^

With threaded MPMs, the database connection pool is shared among all threads of
an Apache child. Thus, it makes sense to tune the size of the pool to the
number of processes and threads. The following should fit the above Apache
dimensions quite well::

    DBDMin  0
    DBDMax  12
    DBDKeep 3
    DBDExptime 10


The total number of connections that Apache might open needs to be reflected in
the PostgreSQL setup accordingly. The above example would be served safely with
a database with a ``max_connections = 500`` setting. (This setting may seem far
too high, but it keeps even enough headroom to start separate Apache servers
for testing purposes, or for upgrade purposes (killing Apache with SIGWINCH for
graceful stop, and starting a new one while the old one still continues to
serve requests to their end).


Thread stack size
^^^^^^^^^^^^^^^^^

When using lots of threads, their might be funny effects on some platforms. The
default stack size allocated by the operating system for threads might be quite
large, e.g. 8 MB on Linux. If this leads to problems, you could considerably
decrease the stack size as such::

    # If this isn't set, the OS' default will be used (8 MB)
    # which is way more than necessary
    ThreadStackSize 1048576

But normally the default settings should just work, I guess.


HTTP/1.1 KeepAlive
^^^^^^^^^^^^^^^^^^

KeepAlive, a HTTP/1.1 feature, saves overhead by reusing already existing TCP
connections to process further HTTP requests. If no additional request arrives
after n seconds, the server closes the connection.

This is a good thing, but it can also become a problem when too many
threads/processes linger around waiting for the next request in the connection.
Each such thread would occupy a slot that could otherwise be used to handle
other requests. In addition, even the number of unused ephemeral ports could
become scarce under extreme conditions. The configured default of a KeepAlive
time of 15 seconds in most Apache installs is far more than necessary. A good
value is 2 seconds, which keeps the good side of KeepAlive, but avoids the
drawbacks to the extent that they don't tend to be a problem.

Hence, good settings are::

    KeepAlive On
    MaxKeepAliveRequests 100
    KeepAliveTimeout 2



.. _tuning_postgresql:

Tuning PostgreSQL
-----------------


To tune PostgreSQL for good performance, you should tweak some or all of the
parameters below in :file:`postgresql.conf`.

This config file often lives in the same directory as the PostgreSQL database,
which would be :file:`/var/lib/pgsql/data` on an openSUSE system -- or it could
be in :file:`/etc`, as in :file:`/etc/postgresql/8.3/main` on Debian Lenny.


Memory sizing
^^^^^^^^^^^^^

.. describe:: shared_buffers

   Make sure to reserve enough memory for the database, especially if it will
   be large. As a rough first estimate, it is usually sufficient and optimal if
   the reserved RAM is about the same as the database size on disk.
   
   There is a special command :program:`mb db sizes` that helps you to assess
   the size of your databases. See :ref:`mb_db_sizes`.

.. FIXME: describe in detail


Other parameters
^^^^^^^^^^^^^^^^

.. describe:: synchronous_commit

   In any case, you should disable the synchronous commit mode
   (synchronous_commit = off). The only case where you don't want that is if
   you have other databases than MirrorBrain, which require a higher level of
   data integrity than MirrorBrain does.

   
.. describe:: listen_addresses

   You'll need to change the parameter listen_addresses if you 

   a) run the web server on a different host than the database server, or if you 
    
   b) want to use the :program:`mb` admin tool from a different host than the the
      database host.

   The default is localhost only. Add '*' or comma-separated addresses.

