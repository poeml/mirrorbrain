
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
        vacuum         clean up unreferenced files from the mirror database


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
