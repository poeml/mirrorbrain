Enabling memcache support for mirror stickiness
===============================================

Memcache support can be compiled into mod_mirrorbrain to implement "mirror
stickiness".  Mirror stickiness means that the server tracks the client-mirror
association. This may be useful to prevent clients from getting redirected to
random mirrors, and rather give them the same mirror for each request.

The feature is implemented using a memcache daemon.

.. note:: This is an optional feature, which you should not need normally. 

When compiling with memcache support, ``-DWITH_MEMCACHE`` needs to be among the
compile flags for mod_mirrorbrain::

    apxs2 -c -Wc,"-DWITH_MEMCACHE -Wall -g" mod_mirrorbrain.c

If you use an apr-util version prior to 1.3, the memcache client support isn't included. Then you'll
need to get and build libapr_memcache, and then you'll probably build mod_mirrorbrain like this::

    apxs2 -c -I/usr/include/apr_memcache-0 -lapr_memcache '-Wc,-Wall -DWITH_MEMCACHE -g -D_GNU_SOURCE' mod_mirrorbrain.c


Further steps required for memcache support would be:

* install memcached
* make sure it listens on localhost only (:file:`/etc/sysconfig/memcached` on openSUSE)
* start it: :command:`/etc/init.d/memcached start`
* configure it to start at boot: :command:`chkconfig -a memcached`
* install mod_memcache from http://code.google.com/p/modmemcache/
  (openSUSE package apache2-mod_memcache)
* load mod_memcache: :command:`a2enmod memcache`

Configuration example::

    <IfModule mod_memcache.c>
        MemcacheServer 127.0.0.1:11211 min=0 smax=4 max=16 ttl=600
    </IfModule>
    <IfModule mod_mirrorbrain.c>
        MirrorBrainMemcached On
        MirrorBrainMemcachedLifetime 1800
    </IfModule>


