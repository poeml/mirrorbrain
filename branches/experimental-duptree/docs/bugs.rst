Known problems
==============

Database reconnections issues
-----------------------------

If the database goes away (or is restarted), it is not clear at the moment if
the pgsql database adapter used by Apache's DBD library reconnects cleanly.
Sometimes it seems that it doesn't reconnect, or that at least some of the
Apache children don't do it, and a graceful restart of Apache is needed. This
needs further inspection.


Passwords containing spaces
-----------------------------

Passwords containing spaces in :file:`/etc/mirrorbrain.conf` are known not to work.


Sporadic corruption of ASN and PFX variables in the subprocess environment
--------------------------------------------------------------------------

mod_asn lookup data is sometimes garbled (subprocess env table)::

    87.79.141.235 - - [16/Feb/2009:14:41:38 +0100] "GET /factory/repo/oss/suse/setup/descr/packages.gz HTTP/1.1" 200 2416300 "-" "ZYpp 5.25.0 (curl 7.19.0)" - r:- 145 2416594 -:- ASN:8422 P:87.78.0.0/15 size:- - - "-"
    87.79.141.235 - - [16/Feb/2009:14:41:38 +0100] "GET /factory/repo/oss/suse/setup/descr/patterns HTTP/1.1" 200 164 "-" "ZYpp 5.25.0 (curl 7.19.0)" - r:- 142 431 -:- ASN:{&\x80\x02 P: size:- - - "-"
    87.79.141.235 - - [16/Feb/2009:14:41:39 +0100] "GET /factory/repo/oss/license.tar.gz HTTP/1.1" 200 24492 "-" "ZYpp 5.25.0 (curl 7.19.0)" - r:- 131 24782 -:- ASN:8422 P:87.78.0.0/15 size:- - - "-"

As a further data point, the following has been seen::

    90.182.x.x - - [20/Jul/2009:12:16:19 +0200] "GET /distribution/10.3/repo/oss/content HTTP/1.1" 302 348 "-" "Novell ZYPP Installer" ftp.linux.cz r:country 170 901 EU:CZ ASN:z,ne,ng,re,rw,sc,sd,sh,sl,sn,so,st,td,tf,tg,tn,tz,ug,yt,za,zm,zw a2 ge,kz,ru P:90.180.0.0/14 size:44325 - - "-"

This further points to a memory corruption, happening while or after
mod_mirrorbrain is running. It seems very hard to trigger though, I'd estimate
20 requests out of 10.000.000.

Grepping logs for ``xaa`` yields some places for examination::

    zcat /var/log/apache2/download.opensuse.org/2009/07/download.opensuse.org-20090720-access_log.gz| grep 'xaa'

Something might be corrupting data in the env table. It doesn't seem to affect
other env variables though, only the two that are set by mod_asn.

All places where the subprocess env table is manipulated in mod_asn and
mod_mirrorbrain seem safe.

A "soft" way of debugging would be to add a detailed debugging logging coupled
to a trigger which pulls when non-numeric data is seen in ``ASN`` or ``PFX``.
When actually adding several such triggers at different stages of request
processing, it should be possible to pinpoint the corruption. In addition, a
core dump of the process could be saved to disk while running.

The bug doesn't seem to have noticeable negative consequences except the messed
up logging of the two values.


Inaccurate logging of ASN lookup data for 404s
----------------------------------------------

It was noticed that for 404s the lookup is correctly done (can be seen in the
response headers), but ``ASN:- P:-`` is being logged.

Empty logging also happens for 200s, when e.g. requesting something from ``/icons``, 
so that fits the configuration::

    87.79.141.235 - - [16/Feb/2009:15:55:43 +0100] "GET /icons/torrent.png HTTP/1.1" 200 3445 "http://download.opensuse.org/distribution/11.1/iso/" "Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-us) AppleWebKit/525.27.1 (KHTML, like Gecko) Shiira Safari/125" - r:- 405 3744 -:- ASN:- P:- size:- - - "-"


But what about redirections exceptions, like size? Example::

  78.34.103.82 - - [16/Feb/2009:16:05:54 +0100] "GET /distribution/11.1/repo/oss/suse/setup/descr/patterns HTTP/1.1" 200 170 "-" "ZYpp 5.24.5 (curl 7.19.0)" - r:- 152 448 -:- ASN:8422 P:78.34.0.0/15 size:- - - "-"


Inaccurate logging of ASN lookup data for overridden AS
-------------------------------------------------------

When the AS is overridden with a query parameter, the one being logged/put into
the headers is not the overriding one.


Wrong reporting by the scanner about symlinks
---------------------------------------------

When scanning via rsync, the scanner reports symlinks (which are mode 0777) as
world-writable directories::

    lrwxrwxrwx 1 root root 13 2009-03-01 05:29 /mounts/dist/unpacked/head-i586/usr/bin/pnmnoraw -> pnmtoplainpnm*

    rsync dir: 777         13 Sun Mar  1 05:29:31 2009  unpacked/head-i586/usr/bin/pnmnoraw

That doesn't harm, because the symlinks are (intentionally) ignored anyway, but
it clutters the output and of course is confusing.


Pondering a hard dependency on mod_geoip
----------------------------------------

One could argue that mod_geoip should be a hard requirement, as mod_form is -
and Apache should check at startup. On the other hand, it could be optional,
because mod_mirrorbrain also works without mod_geoip (distributing requests
world-wide, according to mirror priorities). So it could be a valid use case to
run without mod_geoip.  On the other hand, at least a warning should be issued,
so admins get a hint when mod_geoip was simply forgotten. On the other hand, we
could have a check that prevents starting, unless geoip usage is explicitely
disabled (e.g. MirrorBrainRequireGeoIP off)

Update: explicitely disabling GeoIP would somehow conflict with the `"no GeoIP"
use case`_.

.. _`"no GeoIP" use case`: http://mirrorbrain.org/docs/configuration/#using-mod-mirrorbrain-without-geoip

Mirror list shows wrong region when overridden with query parameter
-------------------------------------------------------------------

Since "override countries" (overridden via query paramter) are not resolved
into region, a wrong region is given in generated mirror lists::

    http://download.opensuse.org/distribution/11.1/repo/oss/suse/noarch/rubygem-rails-2.1.1-1.14.noarch.rpm?mirrorlist&country=ZA
    Found 2 mirrors which handle this country (ZA): <- ok
    Found 61 mirrors in other countries, but same continent (EU): <- wrong


Mirror list gives inaccurate "number of mirrors", if mirrors were excluded
--------------------------------------------------------------------------

The mirrorlist gives inaccurate readings for "number of mirrors", if some
mirrors where not considered, because they are configured ``country-only`` or
``region-only`` (``same_region=1`` or ``same_country=1``)

As further effect of this bug, it was noticed that a mirror is missing from the
?mirrorlist mirror lists if it is configured as fallback mirror for a country::

    http://download.opensuse.org/repositories/KDE:/KDE4:/STABLE:/Desktop/openSUSE_11.1/KDE4-DEVEL.ymp?mirrorlist&country=tw

ftp5 disappears from the list, when configured as fallback for Taiwan. It is
correctly used though and appears on the list *when* actually used as fallback.


``mb file ls`` crashes if probing for files that don't exist in the database
----------------------------------------------------------------------------

If globbing in the database for a file that doesn't exist, with the ``--probe``
option, probing shouldn't actually be attempted. The tool tries nevertheless
and crashes::

     % mb file ls '*libqt4-debuginfo-4.5.2-51.1.x86_64.rpm' -u --md5     
    Traceback (most recent call last):
      File "/suse/poeml/bin/mb", line 1123, in <module>
        sys.exit( mirrordoctor.main() )
      File "/usr/lib64/python2.5/site-packages/cmdln.py", line 257, in main
        return self.cmd(args)
      File "/usr/lib64/python2.5/site-packages/cmdln.py", line 280, in cmd
        retval = self.onecmd(argv)
      File "/usr/lib64/python2.5/site-packages/cmdln.py", line 412, in onecmd
        return self._dispatch_cmd(handler, argv)
      File "/usr/lib64/python2.5/site-packages/cmdln.py", line 1100, in _dispatch_cmd
        return handler(argv[0], opts, *args)
      File "/suse/poeml/bin/mb", line 854, in do_file
        samples = mb.testmirror.lookups_probe(rows, get_digest=opts.md5, get_content=False)
      File "/suse/poeml/mirrorbrain/mirrordoctor/mb/testmirror.py", line 201, in lookups_probe
        return probes_run(probelist)
      File "/suse/poeml/mirrorbrain/mirrordoctor/mb/testmirror.py", line 228, in probes_run
        result = p.map_async(probe_report, probelist)
      File "/usr/lib64/python2.5/site-packages/processing/pool.py", line 186, in mapAsync
        chunksize, extra = divmod(len(iterable), len(self._pool) * 4)
    ZeroDivisionError: integer division or modulo by zero

