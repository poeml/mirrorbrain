.. _tuning:

Tuning guide
============

The following sections describe how to tune PostgreSQL and Apache for good
performance.

Depending on the size of your install, this can be mandatory.


Tuning Apache
-------------

MPM
^^^

In general, it makes sense to use a threaded MPM for Apache. Prefork is less
suitable, because the database connection pool would not be shared across the
worker children. With prefork, each process would open its own connection to
the database. For small installations, this might not matter. However, for a
busy download server, the threaded event MPM or worker MPM are better choices.

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

Hence, good settins are::

    KeepAlive On
    MaxKeepAliveRequests 100
    KeepAliveTimeout 2


.. Tuning PostgreSQL
.. -----------------
.. 
.. To tune PostgreSQL for good performance, you should tweak the following
.. parameters in :file:`postgresql.conf`.
.. 
.. 
.. .. describe:: listen_addresses
.. 
..     asdfasdf asdflkasdasd
