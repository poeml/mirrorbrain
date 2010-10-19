
.. _maintaining_the_mirror_database:

Maintaining the mirror database
===============================


Concepts -- the mb command
--------------------------

:program:`mb` is a commandline tool to do maintain the mirror database, create
mirrors, edit them, work with files and other tasks.

It has several subcommands, and it is typically used in one the following forms::

    mb <command>
    mb <command> <identifier>

A typical example would be::

    mb edit opensuse.uib.no

Note the first argument (after ``edit``), which is the *mirror identifier*. It
serves as a name that uniquely identifies a single mirror. It can be useful if
these identifiers are memorizable by a human. 

For all :program:`mb` commands where a mirror (or several) needs to be
specified, you can abbreviate the identifier by typing part of it. For
instance, instead of::

    mb show opensuse.uib.no

you could just type::

    mb show uib

as long as ``uib`` is uniquely identifying a mirrors among the others.

The :program:`mb` command is extensible. See the developers documentation for
instructions. (To be written yet.)
.. TODO: add reference 


Built-in help
^^^^^^^^^^^^^

:program:`mb` has reference documentation built-in. If you just call
:program:`mb` or :program:`mb -h` or :program:`mb help`, it will print out the
list of known subcommands::

     % mb
    Usage:
        mb COMMAND [ARGS...]
        mb help [COMMAND]
    
    Options:
        --version           show program's version number and exit
        -h, --help          show this help message and exit
        -d, --debug         print info useful for debugging
        -b BRAIN_INSTANCE, --brain-instance=BRAIN_INSTANCE
                            the mirrorbrain instance to use. Corresponds to a
                            section in /etc/mirrorbrain.conf which is named the
                            same. Can also specified via environment variable MB.
    
    Commands:
        commentadd     add a comment about a mirror
        db (vacuum)    perform database maintenance
        delete         delete a mirror from the database
        dirs           show directories that are in the database
        disable        disable a mirror
        edit           edit a new mirror entry in $EDITOR
        enable         enable a mirror
        export         export the mirror list as text file
        file           operations on files: ls/rm/add
        help (?)       give detailed help on a specific sub-command
        instances      list all configured mirrorbrain instances
        iplookup       lookup stuff about an IP address
        list           list mirrors
        markers        show or edit marker files
        mirrorlist     generate a mirror list
        new            insert a new mirror into the database
        probefile      list mirrors on which a given file is present by probing...
        rename         rename a mirror's identifier
        scan           scan mirrors
        score          show or change the score of a mirror
        show           show a mirror entry
        test           test if a mirror is working
        update         update mirrors network data in the database



By typing :program:`mb <command> -h` or :program:`mb help <command>`, help for
the individual command will be printed::

     % mb help list
    list: list mirrors
    
    Usage:
        mb list [IDENTIFIER]
    Options:
        -h, --help          show this help message and exit
        -r XY               show only mirrors whose region matches XY (possible
                            values: sa,na,oc,af,as,eu)
        -c XY               show only mirrors whose country matches XY
        -a, --show-disabled
                            do not hide disabled mirrors
        --disabled          show only disabled mirrors
        --prio              also display priorities
        --asn               also display the AS
        --prefix            also display the network prefix
        --region            also display the region
        --country           also display the country
        --other-countries   also display other countries that a mirror is
                            configured to handle


Creating a new mirror
---------------------

As necessary ingredient, there need to be mirror servers. They need to serve
content via HTTP or FTP. To be scanned, they need to run rsync, FTP or HTTP.
rsync is most efficient for this. FTP is second choice. At last, HTTP may be
used, however it'll work only if the HTTP server provides a reasonable
"standard" directory index.


To make a new mirror known to the database, you use the :program:`mb` command,
specifically the :program:`mb new` subcommand. An example would be the following::

    mb new opensuse.uib.no -H http://opensuse.uib.no/ \
                           -F ftp://opensuse.uib.no/pub/Linux/Distributions/opensuse/ \
                           -R rsync://opensuse.uib.no/opensuse-full/


This creates a new entry in the mirror database with the data provided on the
commandline.

Because providing a lot of data on the commandline can be tiresome, and
incremental changes are often needed to get the data right, there is a command to
edit the data later: :program:`mb edit`.

A new mirror created this way is disabled in the beginning, because it needs to
be scanned first before it can be useful.


Enabling mirror
---------------

Enabling a mirror, or more correctly *enabling redirections* to a mirror, can
be done with the command :program:`mb enable`. 

Before doing this for the first time, the mirror needs to be scanned to be
useful; see below (:ref:`scanning_mirrors`).

Another way to enable a mirror is to edit its database record directly (see
below, where this is explained).


Disabling a mirror
------------------

Using the :program:`mb disable` command, a mirror can be disabled, and
MirrorBrain will immediately stop to send requests to it.

Another way to disable a mirror is to use :program:`mb edit` to edit its
database record, and changing the ``enabled`` field to ``False`` or ``0``. At
the same time, a comment about the reason could be left in the ``comment``
field.

Disabled mirrors are not scanned. Thus, it is usually advisable to scan a
mirror before reenabling it after inactivity for prolonged time, using
:program:`mb scan -e`.

A mirror will also effectively be disabled if the ``score`` is set to ``0``.


Deleting a mirror
-----------------

A mirror is deleted with the :program:`mb delete` command. This command is an
exception of the rule of abbreviating mirror identifiers; here, the full and
exact identifier of the mirror to be deleted must be specified. This is to
prevent typos.

A deleted mirror is permanently pruned from the database upon completion of the
command.


Displaying details about a mirror
---------------------------------

:program:`mb show` will print out the metadata of a mirror. Example::

     % mb show uib
    identifier     : opensuse.uib.no
    operatorName   : UiB - University of Bergen, IT services
    operatorUrl    : http://it.uib.no/
    baseurl        : http://opensuse.uib.no/
    baseurlFtp     : ftp://opensuse.uib.no/pub/Linux/Distributions/opensuse/opensuse/
    baseurlRsync   : rsync://opensuse.uib.no/opensuse-full/
    region         : eu
    country        : no
    asn            : 224
    prefix         : 129.177.0.0/16
    regionOnly     : False
    countryOnly    : False
    asOnly         : False
    prefixOnly     : False
    otherCountries : 
    fileMaxsize    : 0
    publicNotes    : 
    score          : 100
    enabled        : True
    statusBaseurl  : True
    admin          : X, Y, ...
    adminEmail     : mail@example.com
    ---------- comments ----------
    Added - Wed May  6 14:36:10 2009 
    
    *** scanned and enabled at Wed May  6 14:47:56 2009.
    
    Gave stage access.
    poeml, Mon May 11 16:11:56 CEST 2009
    
    Adjusted FTP URL after they switched to stage. (appended "opensuse").
    rsync down at the moment.
    poeml, Mon May 11 17:18:06 CEST 2009
    ---------- comments ----------



A mirror record explained
-------------------------


==============================  ========================================
      Field                       Explanation
==============================  ========================================
.. describe:: identifier        This is the unique identifier of the mirror server. In the table shown by mb edit, this is the only field that cannot be edited. To rename an identifier, you can use the :program:`mb rename` command.
.. describe:: operatorName      The realname of the mirror operator. This could be a person, an the organization running the mirror, or a sponsor. If the mirror list is exposed in some way, this field could be used to give the operator some visibility. Otherwise, it is of no significance than for your information.
.. describe:: operatorUrl       A contact or informative URL.
.. describe:: baseurl           The root HTTP URL of the mirrored file tree on the mirror. Used by the redirector to redirect requests via HTTP. If a mirror doesn't offer HTTP, but only FTP, an FTP URL can be entered here as well.
.. describe:: baseurlFtp        The root FTP URL of the mirrored file tree on the mirror. Used by the scanner to retrieve the file list - if rsync isn't available..
.. describe:: baseurlRsync      The root rsync URL used by the scanner to find the files via rsync. It's possible to use URLs with credentials, like ``rsync://<username>:<password>@<hostname>/module``. rsync is the preferred method of scanning, so it is beneficial if rsync access exists. If it doesn't, the scanner falls back to FTP or HTTP.
.. describe:: region            The region code specifying the continent the mirror server is located in. See also ``regionOnly``. If you create a new mirror, :program:`mb new` tries to fill in this field and the following field for you; it's possible to edit it later, though.
.. describe:: country           The country code for the server. See also ``countryOnly``.
.. describe:: asn               This is optional and is a number of the autonomous system the mirror is located in. It may serve as a more specific "network location" than the country, and is filled in automatically when a mirror is created. If you don't use the autonomous system database together with MirrorBrain, the value will be zero and will be ignored by MirrorBrain. It is not strictly needed. It can also be edited manually, or updated via :program:`mb update --asn <identifier>` from looked up data. *Only meaningful if MirrorBrain is used together with mod_asn*.
.. describe:: prefix            Same as ``asn``, this value is optional, and if present, it is used for a possibly finer-grained mirror selection. It is filled in automatically, and can be edited like asn. Use :program:`mb update --prefix <identifier>` to fill in data from a routing table lookup.
.. describe:: regionOnly        If true, only clients from the same region (continent) as the mirror are redirected to this mirror.
.. describe:: countryOnly       If true, only clients from the same country as the mirror are redirected to this mirror.
.. describe:: asOnly            If true, the mirror will only get requests from clients that are located within the same network autonomous system (using the value in ``asn``).
.. describe:: prefixOnly        If true, the mirror will only get requests from clients that are located within the same network prefix using the value inn ``prefix``).
.. describe:: otherCountries    List of other countries that should be sent to this mirror server. This overrides the country and region choice, and can be used to fine-tune mirror selection. The list of country IDs specified here is given in the form of comma-separated two-letter codes. Apache does a simple string match on these, and a value that would make sense would be ``ca,mx,ar,bo,br,cl,co,ec,fk,gf,gy,pe,py,sr,uy,ve, jp`` for instance.
.. describe:: fileMaxsize       Maximum filesize, the server can deliver without problems (some servers have problems with files > 2GB for example). MirrorBrain automatically checks HTTP servers for correct delivery, so there is no need to define this value for that reason. It can be used, however, to cause only "small" requests to go to certain mirrors, which are known to have too few bandwidth to deliver large files. If you set a threshold here (in bytes), the mirror will only get files that are smaller.
.. describe:: publicNotes       Notes which should be added to a html page listing all mirrors. The field may be used to store information separately from private notes taken in the comments field. The data isn't exposed though, unless you take care of it.
.. describe:: score             The score (priority) of the server. Higher scored servers are used more often than lower scored servers. Default is 100. A server with score=150 will be used more often than a server with score=50.
.. describe:: enabled           Whether a mirror gets requests. Use this to enable redirects to a mirror, or switch them off. Can also be set with :program:`mb enable/disable <identifier>`.
.. describe:: statusBaseurl     This field is edited by the mirror probe each time it runs (which normally is done frequently via cron). If it's true, the mirror probe found that the mirror is alive the last time it looked.
.. describe:: admin             Name of an admin or contact person for the mirror.
.. describe:: adminEmail        Contact Email address.
.. describe:: comments          Free text field for additional comments. Use it in any way that suits you. It lends itself to take notes about communication with mirrors, for instance.
==============================  ========================================


Editing a mirror
----------------

A mirror (in the mirror database) can be edited with the :program:`mb edit` command.

The command will bring up an editor with the mirror's metadata. The
:envvar:`EDITOR` environmental variable is respected, and the editor defaults
to :program:`vim`.

For fields where a Boolean is expected, you can type the value (while editing)
in the form of 0/1 instead of true/false (shorter to type).

When you save the text and close the editor, you'll be asked whether to save
the data to the database.


.. _editing_mirrors_network_location:

Editing a mirrors network location
----------------------------------

There are some fields in the mirror record, for which manual editing doesn't
make so much sense.  These are: 

- country, 
- region,
- autonomous system number,
- network prefix,
- geographical coordinates.

*When a mirror is created (using* :program:`mb new` *), then all these fields are
automatically filled in.* This requires a working DNS lookup and a GeoIP
database. 

The lookup of the autonomous system number and network prefix require
`mod_asn`_ to be configured. 

The geographical coordinates require the GeoIP database to be the `GeoIP city
(lite)`_ version. The smaller database versions don't contain the coordinates.

.. _`GeoIP city (lite)`: http://www.maxmind.com/app/geolitecity


The data can be updated later with the :program:`mb update` command. Regularly
running this command (say, once a month) is a good idea because the data
sometimes might change over time. However, this also means that manual edits
will be overwritten.

The update command can be used for individual mirrors::

     % mb update --coordinates --asn --prefix ftp5
    updating geographical coordinates for ftp5.gwdg.de (0.000 0.000 -> 53.083 8.8)

Or it can be applied to all active mirrors::

     % mb update --coordinates --asn --prefix 
    updating geographical coordinates for ring.yamanashi.ac.jp (0.000 0.000 -> 36.0 138.0)
    updating network prefix for mirror.lupaworld.com (122.224.0.0/12 -> 115.224.0.0/12)
    [...]


Listing mirrors
---------------

:program:`mb list` lists mirrors, with less or more details. In its simplest
form, the command will simply print all identifiers of enabled mirrors.
:program:`mb list -a` includes also the disabled mirrors.

More useful is to add filters, or display more data.

Examples of filtering by country code (here: Bulgaria, ``bg``)::

     % mb list -c bg                  
    mirrors.netbg.com             
    bgadmin.com                   

Example of filtering by region (here: Oceania, ``oc``), and also displaying the
value of the ``otherCountries`` field for each mirror::

     % mb list -r oc --other-countries
    ftp.iinet.net.au               nz
    mirror.aarnet.edu.au           nz
    mirror.pacific.net.au          nz
    mirror.internode.on.net        nz
    mirror.3fl.net.au              nz
    netspace.net.au                nz
    optusnet.com.au                nz

Example of listing all mirrors in Portugal and showing their ``score`` (their
priority)::

     % mb list -c pt --prio                     
    lisa.gov.pt                    100
    ftp.isr.ist.utl.pt              50
    uminho.pt                       50
    ftp.nux.ipb.pt                   3

Showing priority, network prefix and autonomous system of Chinese mirrors::

     % mb list -c cn --prio --as --prefix                 
    mirror.lupaworld.com           100  4134 122.224.0.0/12     
    lizardsource.cn                 30  9389 211.166.8.0/21     
    lcuc.org.cn                    100 17816 218.249.128.0/17   



When *not* filtering the output, the ``--country`` and ``--region`` commandline
options are useful, because they add that data into the output. An example
would be listing all mirrors with the command :program:`mb list --prio --as
--prefix --country --region`.


.. _scanning_mirrors:

Scanning mirrors
----------------

Mirrors need to be scanned for their file lists. This is done with the
:program:`mb scan` command. The program will try rsync, if available, FTP if
not, or HTTP if it's the only option.

An individual mirror can be scanned like this::

     % mb scan roxen
    Fri Jul 31 21:31:50 2009 roxen.integrity.hu: starting
    Fri Jul 31 21:31:51 2009 roxen.integrity.hu: total files before scan: 17248
    Fri Jul 31 21:31:59 2009 roxen.integrity.hu: scanned 17248 files (1935/s) in 8s
    Fri Jul 31 21:31:59 2009 roxen.integrity.hu: files to be purged: 0
    Fri Jul 31 21:32:00 2009 roxen.integrity.hu: total files after scan: 17248
    Fri Jul 31 21:32:00 2009 roxen.integrity.hu: purged old files in 1s.
    Fri Jul 31 21:32:00 2009 roxen.integrity.hu: done.
    Completed in 9 seconds

After creation of a new mirror, it is disabled first. A typical workflow would
be to scan it, after creating it, and then enabling redirection. :program:`mb
scan` command can be used with the ``-e``/``--enable`` option to make this
happen. If the scan went successfully, the mirror will be enabled afterwards::

     % mb scan -e tuwien
    Fri Jul 31 21:50:45 2009 gd.tuwien.ac.at: starting
    Fri Jul 31 21:50:45 2009 gd.tuwien.ac.at: total files before scan: 712
    Fri Jul 31 21:50:46 2009 gd.tuwien.ac.at: scanned 712 files (511/s) in 1s
    Fri Jul 31 21:50:46 2009 gd.tuwien.ac.at: files to be purged: 0
    Fri Jul 31 21:50:46 2009 gd.tuwien.ac.at: total files after scan: 712
    Fri Jul 31 21:50:46 2009 gd.tuwien.ac.at: purged old files in 0s.
    gd.tuwien.ac.at: now enabled.
    Fri Jul 31 21:50:46 2009 gd.tuwien.ac.at: done.
    Completed in 1 seconds



To scan all enabled mirrors in parallel, you would use ``-j``/``--jobs=N``
option to specify the number of scanners to start in parallel, and the
``-a``/``--all`` option::

     % mb scan -j 16 -a

This is likely what you would configure to be done periodically by cron.

To scan only a subdirectory on the mirrors, the ``-d`` option can be used. This
can be useful when it is known that content has been added or removed in
particular places of large trees, in the following example shown with a single
mirror only::

     % mb scan -d repositories/Apache ftp5  
    Checking for existance of 'repositories/Apache' directory
    .
    Scheduling scan on:
        ftp5.gwdg.de
    Completed in 0 seconds
    Fri Jul 31 21:41:37 2009 ftp5.gwdg.de: starting
    Fri Jul 31 21:41:38 2009 ftp5.gwdg.de: files in 'repositories/Apache' before scan: 780
    Fri Jul 31 21:41:40 2009 ftp5.gwdg.de: scanned 780 files (636/s) in 1s
    Fri Jul 31 21:41:40 2009 ftp5.gwdg.de: files to be purged: 0
    Fri Jul 31 21:41:42 2009 ftp5.gwdg.de: total files after scan: 760122
    Fri Jul 31 21:41:42 2009 ftp5.gwdg.de: purged old files in 2s.
    Fri Jul 31 21:41:42 2009 ftp5.gwdg.de: done.
    Completed in 4 seconds


For debugging purposes, the ``-v`` option is useful. It can be repeated several
times to enable more output.



Listing files
-------------

Files known to the database can be listed with the :program:`mb file ls` command.
When specifying a path name, the leading slash is optional and not relevant.
(Internally, the filenames are stored without.)

Example::

     % mb file ls /distribution/11.1/repo/oss/suse/ppc/tcsh-6.15.00-93.3.ppc.rpm        
    as th  100 ok       ok   mirror.in.th                   
    eu at  100 disabled dead tugraz.at                      
    eu at  100 ok       ok   gd.tuwien.ac.at                
    eu de  100 ok       ok   ftp5.gwdg.de                   
    eu hu  100 ok       ok   roxen.integrity.hu             


Globbing can be used. Then, to get more than a list or mirrors, but also the
filenames, the ``-u``/``--url`` option is useful::

     % mb file ls \*.iso -u
    as th  100 ok       ok   mirror.in.th                    http://mirror.in.th/opensuse/ppc/factory/iso/openSUSE-NET-ppc-Build0137-Media.iso
    as th  100 ok       ok   mirror.in.th                    http://mirror.in.th/opensuse/ppc/factory/iso/openSUSE-Factory-NET-ppc-Build0051-Media.iso
    as th  100 ok       ok   mirror.in.th                    http://mirror.in.th/opensuse/ppc/factory/iso/openSUSE-Factory-NET-ppc-Build0059-Media.iso
    as th  100 ok       ok   mirror.in.th                    http://mirror.in.th/opensuse/ppc/factory/iso/openSUSE-NET-ppc-Build0116-Media.iso
    eu de  100 ok       ok   ftp5.gwdg.de                    http://ftp5.gwdg.de/pub/opensuse/ppc/factory/iso/openSUSE-NET-ppc-Build0179-Media.iso
    eu hu  100 ok       ok   roxen.integrity.hu              http://roxen.integrity.hu/pub/opensuse/ppc/factory/iso/openSUSE-NET-ppc-Build0179-Media.iso


In addition to just listing what's known to the database, the command can also
do probing. The number is the HTTP return code (200 for OK)::

     % mb file ls /distribution/11.1/repo/oss/suse/ppc/tcsh-6.15.00-93.3.ppc.rpm --probe
    .....
    as th  100 ok       ok   mirror.in.th                    200
    eu at  100 disabled dead tugraz.at                          
    eu at  100 ok       ok   gd.tuwien.ac.at                 200
    eu de  100 ok       ok   ftp5.gwdg.de                    200
    eu hu  100 ok       ok   roxen.integrity.hu              200


When used with probing, there is the additional option to actually download the
content and display a checksum of what was returned::

     % mb file ls --probe /distribution/11.1/repo/oss/suse/ppc/tcsh-6.15.00-93.3.ppc.rpm --md5
    .....
    as th  100 ok       ok   mirror.in.th                    200 50dc50b20a97783a51ff402359456e3a
    eu at  100 disabled dead tugraz.at                                                           
    eu at  100 ok       ok   gd.tuwien.ac.at                 200 50dc50b20a97783a51ff402359456e3a
    eu de  100 ok       ok   ftp5.gwdg.de                    200 50dc50b20a97783a51ff402359456e3a
    eu hu  100 ok       ok   roxen.integrity.hu              200 50dc50b20a97783a51ff402359456e3a

To be usable with lots of mirrors, the probing is done in parallel.


The :program:`mb file` command can also be used as :program:`mb file add` and
:program:`mb file rm` to manipulate the database. See the help output of the
command for details.



Exporting mirror lists
----------------------

The :program:`mb export` command can export data from the mirror database in
several different formats, for different purposes.


.. _export_mirmon:

Exporting in mirmon format
^^^^^^^^^^^^^^^^^^^^^^^^^^

`mirmon`_ is a program written by Henk P. Penning which monitors the status of mirrors.
The format "mirmon" exports a list of mirrors in a text format that can be read
by mirmon.  

.. _`mirmon`: http://people.cs.uu.nl/henkp/mirmon/

With this, it is straighforward to deploy mirmon and automate it to use the
mirrors from the database. Thus, no separate list of mirrors needs to be
maintained for it.

The command ``mb export --format=mirmon`` generates the list that mirmon needs,
which basically looks like this::


     % mb export --format=mirmon | head
    de      http://ftp-stud.fht-esslingen.de/pub/Mirrors/ftp.opensuse.org/  <...@...>
    de      ftp://ftp-stud.fht-esslingen.de/pub/Mirrors/ftp.opensuse.org/   <...@...>
    de      rsync://ftp-stud.fht-esslingen.de/opensuse/     <...@...>
    us      http://mirror.anl.gov/pub/opensuse/opensuse/    <...@...>
    us      ftp://mirror.anl.gov/pub/opensuse/opensuse/     <...@...>
    us      rsync://mirror.anl.gov/opensuse/opensuse/       <...@...>
    ...


To give a full example, here's how the actual mirmon config file would look
like. Note the ``mirror_list`` line which pulls the generated list in::

    project_name example.org
    project_url http://www.example.org/mirrors/
    mirror_list /usr/bin/mb export --format=mirmon |
    web_page /var/www/example.org/mirmon/index.html
    icons icons
    probe /usr/bin/wget -q -O - -T %TIMEOUT% -t 1 %URL%timestamp.txt
    state /home/mirrorbrain/mirmon/state
    countries /usr/local/mirmon-2.3/countries.list
    project_logo http://www.example.org/images/logo.gif
    list_style plain
    timeout 20


The cron job to create the list and run mirmon would look like this::

    30 * * * *   mirrorbrain    perl /usr/local/mirmon-2.3/mirmon -q -get update -c /etc/mirmon.conf

Note: when mirmon is run for the first time, the state file needs to be
touched, or the script will not run.

The icons which are included in the resulting HTML page need to made available by Apache::

    Alias /mirmon/icons /usr/local/mirmon-2.3/icons
    <Directory /usr/local/mirmon-2.3/icons>
        Options None
        AllowOverride None
        Order allow,deny
        Allow from all
    </Directory>


Further tips:

1) If your mirmon is configured with ``list_style apache`` instead of
   ``list_style plain``, a different mirror list format is needed; use
   :program:`mb export` with the ``mb export --format=mirmon-apache`` option
   then.

2) If you prefer to run :program:`mb export` under a different user id than
   mirmon, you can write the mirror list to an intermediate file, and configure
   mirmon to use the file like this::

     mirror_list /path/to/mirmon/mirrorlist-export



.. _export_subversion:

Exporting to a Version Control System (VCS)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Exporting data in text format is a dead easy way to keep a history of changes
that happen in the mirror database — and mail them around, so everybody
involved is kept updated. At the same time, it serves archival purposes.

The idea is to export snapshots of the data in text format. The resulting files
are put into a standard version control system, and standard post-commit hook
scripts can be used to trigger certain actions (e.g. email). 

The resulting archive of changes is all human-readable (much more useful than
raw database backups). The changes can actually be mailed around in the form of
a diff, showing some context.

A different way to implement a notification system for mirror changes would be
to notify about each and every change done to the database — however, often
changes have to be done incrementally and this would be a noisy method when
working on a mirror's configuration. 

Instead, an hourly snapshot is normally sufficient to keep others informed, and
shouldn't be too noisy.

`Subversion`_ is the only version control system supported at the moment, but
should hopefully be ubiquitous enough.

.. _`Subversion`: http://subversion.tigris.org/

To set this up, first a repository needs to be created::

    doozer:~ # su - mirrorbrain
    mirrorbrain@doozer:~> svnadmin create mirrors-svn-repos
    mirrorbrain@doozer:~> svn co file://$PWD/mirrors-svn-repos mirrors-svn
    Checked out revision 0.
    mirrorbrain@doozer:~> 


Then, set up a cron job to run every hour, calling :program:`mb export` with
the ``--format=vcs`` and the ``--commit=svn`` options. The latter automatically
runs ``svn commit`` after the export (taking into account files that have been
deleted, or occur for the first time)::

     # export mirrordb contents to SVN and send commit mails
    7 * * * *      mirrorbrain   mb export --format vcs --target-dir ~/mirrors-svn --commit=svn

Finally, the post-commit hook script is missing, which takes care of
sending mails. Create and edit it as follows::

    mirrorbrain@doozer:~> touch mirrors-svn-repos/hooks/post-commit
    mirrorbrain@doozer:~> chmod +x mirrors-svn-repos/hooks/post-commit
    mirrorbrain@doozer:~> vi mirrors-svn-repos/hooks/post-commit

    #!/bin/sh
    REPOS="$1"
    REV="$2"
    /usr/share/subversion/tools/hook-scripts/mailer/mailer.py commit "$REPOS" "$REV" /etc/mailer.conf

The path to the :program:`mailer.py` script likely needs adjustment. The
configuration (:file:`/etc/mailer.conf`) could look like this::

    [general]
    mail_command = /usr/sbin/sendmail

    [defaults]
    diff = /usr/bin/diff -u -L %(label_from)s -L %(label_to)s %(from)s %(to)s
    generate_diffs = add copy modify
    show_nonmatching_paths = yes
    
    [mirrordb]
    for_repos = /home/mirrorbrain/mirrors-svn-repos
    from_addr = mirrorbrain@...
    to_addr = admin@foo bar@...
    commit_subject_prefix = [mirrordb]
    propchange_subject_prefix = [mirrordb]



Exporting in PostgreSQL format
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The format "postgresql" creates SQL INSERT statements that can be run on a
PostgreSQL database. This can e.g. be used to migrate the data into another
database.

The resulting dump could be loaded into a mirrorbrain instance like this::

    mb db shell < db.dump


Exporting in Django format
^^^^^^^^^^^^^^^^^^^^^^^^^^

This is experimental stuff — intended for hacking on the `Django`_ web
framework. Data is exported in the form of Django ORM objects, and the export
routine will very likely need modification for particular purposes. The
existing code has been used to experiment with. Get in contact if you are
interested in hacking on this!

.. _`Django`: http://www.djangoproject.com/


Performing database maintenance
-------------------------------

The :program:`mb db` command offers some helpful functionality regarding
database maintenance. It has several subcommands.


Regular cleanups with :program:`mb db vacuum`
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This command cleans up unreferenced files from the mirror database.

This should be done once a week for a busy file tree.  Otherwise it should be
rarely needed, but can possibly improve performance if it is able to shrink the
database.

When called with the ``-n`` option, only the number of files to be cleaned up
is printed, so it's purely for information. No cleanup is performed.

The recommended cron job looks like this::

    # Monday: database clean-up day...
    30 1 * * mon              mirrorbrain   mb db vacuum

Note: This functionality is not to be confused with the PostgreSQL-internal
vacuuming, which typically happens automatic these days (8.x), but was a manual
process at some time in the past.


Database shell with :program:`mb db shell`
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

With this command, you can conveniently open a database shell::

     % mb db shell
    psql (8.4.1)
    Type "help" for help.
    
    mb_opensuse=> 

...ready to enter commands in psql, the `PostgreSQL interactive terminal`_.

.. _`PostgreSQL interactive terminal`: http://www.postgresql.org/docs/8.4/static/app-psql.html


.. _mb_db_sizes:

Database size info with :program:`mb db size`
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The command :program:`mb db size` prints the size of each database relation.
(In PostgreSQL speak, a *relation* is a table or an index.) This provides
insight for appropriate database tuning and planning. Here's an example::

     % mb db sizes       
    Size(MB) Relation
    464.5    filearr
    532.9    filearr_path_key
     74.3    filearr_pkey
     23.8    pfx2asn
     30.1    pfx2asn_pfx_key
     19.9    pfx2asn_pkey
      0.0    pg_foreign_server
      0.0    pg_foreign_server_name_index
      0.0    pg_foreign_server_oid_index
      0.0    pg_user_mapping_user_server_index
      0.2    server
      0.0    server_enabled_status_baseurl_score_key
      0.0    server_identifier_key
      0.0    server_pkey
      0.0    sql_sizing_profiles
    Total: 1145.9

This example shows a really, really large database, containing nearly 3
millions (!) of files. It uses a good gigabyte of disk space.

``filearr`` contains the file names and associations to the mirrors.
``filearr_path_key`` is the index on the file names. ``filearr_pkey`` is the
primary key. These will be the largest things in a database filled with
millions of files.

The ``pfx*`` relations are only present when `mod_asn`_ is installed. The size
they use is always the same.

.. _`mod_asn`: http://mirrorbrain.org/mod_asn/


