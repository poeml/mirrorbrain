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

Having said that, if you find that you need a hundred connections or more in an
everyday situation, there is something wrong -- then you need to check if you
chose the Apache threading model, check for the size of the database connection
pool, and verify that there are no big bottlenecks in the database (which
causes Apache to stall and stack up working threads and connections).


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


.. note::
   When it is getting *really* serious, like when you are slashdotted, don't
   hesitate to simply switch KeepAlive off. You will see a drastic improvement,
   and probably save you. (If you did your homework and your website is lean
   and fast otherwise ;-) If it is fat and bloated, there is not much to do.)


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

With a small database, using only a few megabytes, there will not be much need
for tuning. With larger databases, that go into the hundreds of megabytes,
tuning becomes important.

.. note::

   Make sure to reserve enough memory for the database, especially if it will
   be large. As a rough first estimate, it is usually sufficient and optimal if
   the reserved RAM is about the same as the database size on disk.
   
Allocating memory to the database is done in the following way. PostgreSQL
largely relies on buffer caching done by the OS. Thus, the first measure in
"reserving" memory is to *not* run too much other stuff on the machine, which
would compete for memory, or (in other words) having enough memory. In general,
PostgreSQL's performance reaches its maximum when the whole dataset, including
indexes, fits in to the amount of RAM available for caching. (That statement is
true if the whole dataset is actually used -- if only parts are used, top
performance will be reached already with less memory. MirrorBrain tends to use
the whole dataset, at least during mirror scanning.)

There is a special command :program:`mb db sizes` that helps you to assess
the size of your databases. See :ref:`mb_db_sizes`. (Just note that changes may
not be immediately reflected in the numbers, because the statistics are updated
periodically by PostgreSQL.)


.. describe:: shared_buffers

   This parameter should usually be set to about 10-25% of the available RAM. 
   Maximum value may be limited by the SHMMAX tunable of the OS. 

   (Of course, if your database is only 10 MB in size, there is no benefit in
   increasing this value that far. It obviously depends on the database size.)

   Mirror scanning can incur heavy write activity, if there is a lot of
   fluctuation in the file tree, and when done in a massive parallel way.
   Scanning performance can benefit from higher values (25-50%) here. For read
   performance, (as affecting Apache and mod_mirrorbrain) higher values are not
   needed.

.. describe:: effective_cache_size

   This is the effective amount of caching between the actual PostgreSQL
   buffers, and the OS buffers.

   This does not create RAM allocations nor does it change how PostgreSQL uses
   RAM -- it just gives PostgreSQL an assumption about the availability of
   memory to the OS cache. This influences decisions in the query planner,
   regarding usage of indexes.

   In principle, this value could be set to the sum of ``cached + free`` in the
   :program:`free -m` output. However, this value needs to be divided by the
   number of processes using this memory simultaneously. To estimate the
   latter, you could use :program:`top` to see how many PostgreSQL processes
   are busy at the same time.

   Anyway, it is better to set this parameter too low rather than too high,
   because that could result in too many index scans.


Other memory parameters that you might want to increase are:

- ``maintenance_work_mem`` (generously)
- ``work_mem`` (a bit)


Connection setup
^^^^^^^^^^^^^^^^

.. describe:: listen_addresses

   You'll need to change the parameter listen_addresses if you 

   a) run the web server on a different host than the database server, or if you 
    
   b) want to use the :program:`mb` admin tool from a different host than the the
      database host.

   The default is localhost only. Add '*' or comma-separated addresses.


.. describe:: max_connections

   The default of 100 should fit many cases. Apache's re-usal of connections is
   so efficient (and MirrorBrain quickly done with answering queries) that a
   handful connections is enough. However, if you use Apache's prefork MPM,
   every child will use a connection. Thus, if you allow to have 200 Apache
   processes running you will need to adjust max_connections accordingly. With
   a threaded Apache, the connection pool is shared, so no problem. This is
   further discussed above, in the notes regarding Apache tuning.



Transaction log
^^^^^^^^^^^^^^^

The transaction log (called Write-Ahead-Log or WAL) is a central thing in
PostgreSQL, and the configuration of its handling important.

.. describe:: synchronous_commit

   In any case, you should disable the synchronous commit mode
   (synchronous_commit = off). The only case where you don't want that is if
   you have other databases than MirrorBrain, which require a higher level of
   data integrity than MirrorBrain does.


.. describe:: wal_buffers

   The default (64kB) may be increased to e.g. 256kB.


.. describe:: checkpoint_segments

   For big databases (hundreds of MB in size), increase this from 3 to 32.


.. describe:: checkpoint_timeout

   Increase to 15min.


To log a checkpoint whenever one occurs, set ``log_checkpoints = on`` and
``checkpoint_warning to 1h``.  


Deadlocks
^^^^^^^^^

.. describe:: deadlock_timeout

   As described below, set this parameter to 30s.

Concurrent write access by different processes to the same rows causes a
queue-up of those write-requests. A row can be written only by one process at a
time. If a process waits too long, it gives up after a while. Its lock times
out, so to speak, which is called a deadlock in this context. It's not a real
deadlock in the common sense, it's just giving up after a while.

Read activity (as done by Apache + mod_mirrorbrain, serving users) is not
affected by write activity locks. Write activity is mainly caused by mirror
scanning. Scanning then again is often done in parallel, to save time, so it is
typical to have to wait for locks (when two scanners happen to want to write to
similar regions in the database).

The default time waiting for a lock is 1s in PostgreSQL, which is often too
short for MirrorBrain. That could be too long for other applications in fact,
but for a mirror scanner it doesn't matter if it has to wait many seconds now
and then. In fact, it is best to increase the lock waiting time to something
like 30 seconds. The deadlocks don't occur frequently when scanning, but when
they occur, you don't want a scanner to give up on that part and have some
missing files on the mirror later.

Such deadlocks are more likely to occur when scanning a new mirror, which
means that every database row has to be touched (for each file found on the
mirror). Even more likely (actually, unavoidable) are they when you fill your
database for the first time, after installing, and the rows are created at the
first place. In that case, you will see deadlocks occur frequently. The best
advice is to ignore them and simply scan once again, after the first run has
completed.

Later scans mainly see what they know already, so there is no reason to write
to the database, which means that deadlocks don't occur.


Enhancing logging
^^^^^^^^^^^^^^^^^

Logging can be enhanced with some details that might be relevant or helpful to
tuning for MirrorBrain:


.. describe:: log_line_prefix

   To get more detailed log lines, set it to::
        
        '%m [%p:%l] %u@%d '

   Note the trailing space!

.. describe:: log_lock_waits

   To log lock waits >= deadlock_timeout, set to ``on``.

.. describe:: log_min_duration_statement

   You could log all long-running queries by configuring this to e.g. ``2000``
   (value is in milliseconds).




