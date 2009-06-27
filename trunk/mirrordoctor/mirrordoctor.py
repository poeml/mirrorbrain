#!/usr/bin/python
# encoding: utf-8

"""
Script to maintain the mirror database

Requirements:
cmdln from http://trentm.com/projects/cmdln/

Install via e.g.
easy_install http://trentm.com/downloads/cmdln/1.1.1/cmdln-1.1.1.zip
(it is not in the Python CheeseShop so far)
"""

__version__ = '1.0'
__author__ = 'Peter Poeml <poeml@suse.de>'
__copyright__ = 'Novell / SUSE Linux Products GmbH'
__license__ = 'GPL'
__url__ = 'http://mirrorbrain.org'



import cmdln
import mb.geoip



# todo: 

# abstractions:
# - append a comment
#   - with timestamp
#
# - select a server from the database

# table changes;
# identifier MUST be unique (schema change required)
# baseurlFtp could be empty, no problem.
# baseurlHttp must not be empty



def lookup_mirror(self, identifier):

    r = mb.conn.servers_match(self.conn.Server, identifier)

    if len(r) == 0:
        sys.exit('Not found.')
    elif len(r) == 1:
        return r[0]
    else:
        print 'Found multiple matching mirrors:'
        for i in r:
            print i.identifier
        sys.exit(1)



class MirrorDoctor(cmdln.Cmdln):

    def get_optparser(self):
        """Parser for global options (that are not specific to a subcommand)"""
        optparser = cmdln.CmdlnOptionParser(self, version=__version__)
        optparser.add_option('-d', '--debug', action='store_true',
                             help='print info useful for debugging')
        optparser.add_option('-b', '--brain-instance', 
                             help='the mirrorbrain instance to use. '
                                  'Corresponds to a section in '
                                  '/etc/mirrorbrain.conf which is named the same. '
                                  'Can also specified via environment variable MB.')
        return optparser


    def postoptparse(self):
        """runs after parsing global options"""

        import os, mb.conf
        if not self.options.brain_instance:
            self.options.brain_instance = os.getenv('MB', default=None)
        self.config = mb.conf.Config(instance = self.options.brain_instance)

        # set up the database connection
        import mb.conn
        self.conn = mb.conn.Conn(self.config.dbconfig, debug = self.options.debug)


    def do_instances(self, subcmd, opts):
        """${cmd_name}: list all configured mirrorbrain instances 

        ${cmd_usage}
        ${cmd_option_list}
        """
        for i in self.config.instances:
            print i


    @cmdln.option('-C', '--comment', metavar='ARG',
                        help='comment string')

    @cmdln.option('-a', '--admin', metavar='ARG',
                        help='admins\'s name')
    @cmdln.option('-e', '--admin-email', metavar='ARG',
                        help='admins\'s email address')

    @cmdln.option('-s', '--score', default=100, metavar='ARG',
                        help='"power index" of this mirror, defaults to 100')

    @cmdln.option('-F', '--ftp', metavar='URL',
                        help='FTP base URL')
    @cmdln.option('-R', '--rsync', metavar='URL',
            help='rsync base URL (starting with rsync://)')
    @cmdln.option('-H', '--http', metavar='URL',
                        help='HTTP base URL')

    @cmdln.option('-r', '--region', metavar='ARG',
                        help='two-letter region code, e.g. EU')
    @cmdln.option('-c', '--country', metavar='ARG',
                        help='two-letter country code, e.g. DE')

    def do_new(self, subcmd, opts, identifier):
        """${cmd_name}: insert a new mirror into the database


        example:
            mirrorbrain.py new example.com \\
                -H http://mirror1.example.com/pub/opensuse/ \\
                -F ftp://mirror1.example.com/pub/opensuse/ \\
                -R rsync://mirror1.example.com/opensuse/ \\
                -a 'He Who Never Sleeps' \\
                -e nosleep@example.com

        ${cmd_usage}
        ${cmd_option_list}
        """

        import time
        import urlparse
        import mb.asn


        if not opts.http:
            sys.exit('An HTTP base URL needs to be specified')

        scheme, host, path, a, b, c = urlparse.urlparse(opts.http)
        if not opts.region:
            opts.region = mb.geoip.lookup_region_code(host)
        if not opts.country:
            opts.country = mb.geoip.lookup_country_code(host)

        r = mb.asn.iplookup(self.conn, host)
        asn, prefix = r.asn, r.prefix
        if not asn: asn = 0
        if not prefix: prefix = ''

        if opts.region == '--' or opts.country == '--':
            raise ValueError('Region lookup failed. Use the -c and -r option.')

        s = self.conn.Server(identifier   = identifier,
                             baseurl      = opts.http,
                             baseurlFtp   = opts.ftp or '',
                             baseurlRsync = opts.rsync or '',
                             region       = opts.region,
                             country      = opts.country,
                             asn          = asn,
                             prefix       = prefix,
                             score        = opts.score,
                             enabled      = 0,
                             statusBaseurl = 0,
                             admin        = opts.admin or '',
                             adminEmail   = opts.admin_email or '',
                             operatorName = '',
                             operatorUrl  = '',
                             otherCountries = '',
                             publicNotes  = '',
                             comment      = opts.comment \
                               or 'Added - %s' % time.ctime(),
                             scanFpm      = 0,
                             countryOnly  = 0,
                             regionOnly   = 0,
                             asOnly       = 0,
                             prefixOnly   = 0)
        if self.options.debug:
            print s


    @cmdln.option('--country', action='store_true',
                        help='also display the country')
    @cmdln.option('--region', action='store_true',
                        help='also display the region')
    @cmdln.option('--prefix', action='store_true',
                        help='also display the network prefix')
    @cmdln.option('--asn', action='store_true',
                        help='also display the AS')
    @cmdln.option('--prio', action='store_true',
                        help='also display priorities')
    @cmdln.option('--disabled', action='store_true',
                        help='show only disabled mirrors')
    @cmdln.option('-a', '--show-disabled', action='store_true',
                        help='do not hide disabled mirrors')
    @cmdln.option('-c', metavar='XY',
                        help='show only mirrors whose country matches XY')
    @cmdln.option('-r', metavar='XY',
                        help='show only mirrors whose region matches XY '
                        '(possible values: sa,na,oc,af,as,eu)')
    # @cmdln.alias('ls') ?
    def do_list(self, subcmd, opts, *args):
        """${cmd_name}: list mirrors

        Usage:
            mb list [IDENTIFIER]
        ${cmd_option_list}
        """
        if opts.c:
            mirrors = self.conn.Server.select("""country LIKE '%%%s%%'""" % opts.c)
        elif opts.r:
            mirrors = self.conn.Server.select("""region LIKE '%%%s%%'""" % opts.r)
        elif args:
            mirrors = mb.conn.servers_match(self.conn.Server, args[0])
        else:
            mirrors = self.conn.Server.select()

        for mirror in mirrors:
            s = []
            s.append('%-30s' % mirror.identifier)
            if opts.prio:
                s.append('%3s' % mirror.score)
            if opts.region:
                s.append('%2s' % mirror.region)
            if opts.country:
                s.append('%2s' % mirror.country)
            if opts.asn:
                s.append('%5s' % mirror.asn)
            if opts.prefix:
                s.append('%-19s' % mirror.prefix)
            s = ' '.join(s)

            if opts.show_disabled:
                print s
            elif opts.disabled:
                if not mirror.enabled:
                    print s
            else:
                if mirror.enabled:
                    print s


    def do_show(self, subcmd, opts, identifier):
        """${cmd_name}: show a mirror entry

        ${cmd_usage}
        ${cmd_option_list}
        """

        mirror = lookup_mirror(self, identifier)
        print mb.conn.server_show_template % mb.conn.server2dict(mirror)


    @cmdln.option('--all-prefixes', action='store_true',
                        help='show all prefixes handled by this AS')
    @cmdln.option('-p', '--prefix', action='store_true',
                        help='print the network prefix')
    @cmdln.option('-a', '--asn', action='store_true',
                        help='print the AS number')
    def do_iplookup(self, subcmd, opts, ip):
        """${cmd_name}: lookup stuff about an IP address

        Requires a pfx2asn table to be present, which can be used to look
        up the AS (autonomous system) number and the closest network prefix
        that an IP is contained in.
        Such a table is probably used in conjunction with mod_asn.
        (Get it. It is worth it ;-)

        ${cmd_usage}
        ${cmd_option_list}
        """
        import mb.asn

        r = mb.asn.iplookup(self.conn, ip)

        if opts.asn:
            print r.asn
        elif opts.prefix:
            print r.prefix
        else:
            print '%s (AS%s)' % (r.prefix, r.asn)
        if opts.all_prefixes:
            r2 = mb.asn.asn_prefixes(self.conn, r.asn)
            print ', '.join(r2)


    @cmdln.option('--all-mirrors', action='store_true',
                        help='update *all* mirrors (also disabled ones)')
    @cmdln.option('-p', '--prefix', action='store_true',
                        help='update the network prefix')
    @cmdln.option('-a', '--asn', action='store_true',
                        help='update the AS number')
    def do_update(self, subcmd, opts, *args):
        """${cmd_name}: update mirrors network data in the database

        Requires a pfx2asn table to be present, which can be used to look
        up the AS (autonomous system) number and the closest network prefix
        that an IP is contained in.
        Such a table is probably used in conjunction with mod_asn.

        The IP to be looked up is derived from the HTTP base URL.

        ${cmd_usage}
        ${cmd_option_list}
        """
        from mb.asn import iplookup
        from mb.util import hostname_from_url
        from sqlobject.sqlbuilder import AND

        #r = mb.asn.iplookup(self.conn, ip)

        #if opts.asn:
        #    print r.asn
        #elif opts.prefix:
        #    print r.prefix
        #else:
        #    print '%s (AS%s)' % (r.prefix, r.asn)

        mirrors = []
        for arg in args:
            mirrors.append(lookup_mirror(self, arg))

        if not args:
            if opts.all_mirrors:
                mirrors = self.conn.Server.select()
            else:
                mirrors = self.conn.Server.select(
                             AND(self.conn.Server.q.statusBaseurl, 
                                 self.conn.Server.q.enabled))

        for mirror in mirrors:
            print mirror.identifier, 
            hostname = hostname_from_url(mirror.baseurl)
            res = iplookup(self.conn, hostname)
            if res: print res
            if res and opts.prefix:
                mirror.prefix = res.prefix
            if res and opts.asn:
                mirror.asn = res.asn


    def do_test(self, subcmd, opts, identifier):
        """${cmd_name}: test if a mirror is working

        This does only rudimentary checking for now. It only does a request
        on the base URL. But more checks could easily be added.
        (See the implementation of do_probefile() for ideas.)

        ${cmd_usage}
        ${cmd_option_list}
        """

        mirror = lookup_mirror(self, identifier)
        print mirror.baseurl
        import mb.testmirror
        print mb.testmirror.access_http(mirror.baseurl)


    @cmdln.option('--content', action='store_true',
                        help='download and show the content')
    @cmdln.option('--md5', action='store_true',
                        help='download and show the md5 sum')
    @cmdln.option('--urls', dest='url_type', metavar='TYPE', default='scan',
                        help='type of URLs to be probed (scan|http|all). Default: scam.')
    @cmdln.option('-m', '--mirror', 
                        help='probe only on this mirror')
    @cmdln.option('-a', '--all-mirrors', action='store_true',
                        help='test also on mirrors which are marked disabled')
    @cmdln.option('-n', '--hide-negative', action='store_true',
                        help='hide mirrors that don\'t have the file')
    def do_probefile(self, subcmd, opts, filename):
        """${cmd_name}: list mirrors on which a given file is present
        by probing them

        The --urls option selects the kind of URLs to be probed. Meanings are:
          'scan' - probes those URLs that would be used in scanning (rsync, 
                   and FTP/HTTP only as fallback). This is fastest, and 
                   suitable for quick probing.
          'http' - probes the base URLs that the clients get to see (those 
                   used in redirection). Gives the most realistic view.
          'all'  - probes all and every URL registered for a host. The most
                   thourough method, which can be useful to discover permission
                   problems on mirrors, serving staged content already where
                   they shouldn't.

        Proxy settings via environmental variables are ignored. 

        Examples:
             mb probefile --md5 update/11.0/rpm/i586/insserv-1.11.0-31.2.i586.rpm
             mb probefile distribution/.timestamp --content --urls=http
             mb probefile distribution/.timestamp -m widehat.opensuse.org

        ${cmd_usage}
        ${cmd_option_list}
        """

        from sqlobject.sqlbuilder import AND
        import mb.testmirror
        import os.path

        mb.testmirror.dont_use_proxies()

        if opts.mirror:
            mirrors = [ lookup_mirror(self, opts.mirror) ]
        elif opts.all_mirrors:
            mirrors = self.conn.Server.select()
        else:
            mirrors = self.conn.Server.select(
                         AND(self.conn.Server.q.statusBaseurl, 
                             self.conn.Server.q.enabled))

        try:
            mirrors_have_file = mb.testmirror.mirrors_have_file(mirrors, filename, 
                                                               url_type=opts.url_type, get_digest=opts.md5,
                                                               get_content=opts.content)
            print
            found_mirrors = 0
            for mirror in mirrors:
                for sample in mirrors_have_file:
                    if mirror.identifier == sample.identifier:

                        s = "%d %-30s" % (sample.has_file, sample.identifier)
                        if opts.md5:
                            s += " %-32s" % (sample.digest or '')
                        s += " %s" % sample.probeurl
                        if sample.http_code:
                            s += " http=%s" % (sample.http_code)
                        print s
                        if opts.content and sample.content:
                            print repr(sample.content)

                        if sample.has_file: found_mirrors += 1

        except KeyboardInterrupt:
            print >>sys.stderr, 'interrupted!'
            return 1

        print 'Found:', found_mirrors



    def do_edit(self, subcmd, opts, identifier):
        """${cmd_name}: edit a new mirror entry in $EDITOR

        Usage:
            mb edit IDENTIFIER
        ${cmd_option_list}
        """
        mirror = lookup_mirror(self, identifier)
        
        import mb.conn
        old_dict = mb.conn.server2dict(mirror)
        old = mb.conn.server_show_template % old_dict

        import mb.util
        boilerplate = """#\n# Note: You cannot modify 'identifier' or 'id'. You can use 'mb rename' though.\n#\n"""
        new = mb.util.edit_file(old, boilerplate=boilerplate)
        if not new:
            print 'Quitting.'
        else:
            new_dict = mb.conn.servertext2dict(new)

            for i in mb.conn.server_editable_attrs:
                if str(old_dict[i]) != new_dict[i]:
                    print """changing %s from '%s' to '%s'""" \
                            % (i, old_dict[i], new_dict[i])
                    a = new_dict[i]
                    if a == 'False': a = False
                    if a == 'True': a = True
                    if type(getattr(mirror, i)) in [type(1L), type(1), bool]:
                        a = int(a)
                    setattr(mirror, i, a)



    @cmdln.option('-e', '--edit', action='store_true',
                        help='edit the markers in $EDITOR')
    @cmdln.option('-d', '--delimiter', default='|',
                        help='use this character (sequence) as delimiter when editing')
    def do_markers(self, subcmd, opts):
        """${cmd_name}: show or edit marker files

        Marker files are used to generate mirror lists.
        Also cf. to the "mirrorlist" command.

        You need to enter files, not directories.

        Usage:
            mb markers 
        ${cmd_option_list}
        """
        markers = self.conn.Marker.select()


        
        old = [ '%s %s %s' \
                % (i.subtreeName, 
                   opts.delimiter, 
                   ' '.join(i.markers.split())) 
                for i in markers ]
        old = '\n'.join(old) + '\n'


        # list only
        if not opts.edit:
            print old
            sys.exit(0)
        

        import mb.util
        boilerplate = """\
#
# Note: %(delim)r delimits subtree name and marker file(s).
# Example:
# 
# Factory %(delim)s factory/repo/oss/content
# PPC     %(delim)s ppc/factory/repo/oss/content
# BS      %(delim)s repositories/server:mail.repo repositories/Apache.repo

""" % { 'delim': opts.delimiter }

        new = mb.util.edit_file(old, boilerplate=boilerplate)

        if not new:
            print 'Quitting.'
        else:

            # delete all markers
            markers = self.conn.Marker.select()
            for i in markers:
                self.conn.Marker.delete(i.id)

            # save the fresh markers
            for i in new.splitlines():
                i = i.strip()
                if not i or i.startswith('#'):
                    continue
                try:
                    name, markers = i.split(opts.delimiter, 1)
                except:
                    sys.exit('Parse error')

                s = self.conn.Marker(subtreeName = name.strip(),
                                     markers = ' '.join(markers.split()))



    def do_delete(self, subcmd, opts, identifier):
        """${cmd_name}: delete a mirror from the database

        ${cmd_usage}
        ${cmd_option_list}
        """
        
        if not identifier:
            sys.exit('need to specify identifier')

        import mb.core
        mb.core.delete_mirror(self.conn, identifier)


    @cmdln.option('-C', '--comment', metavar='ARG',
                        help='comment string to append')
    def do_commentadd(self, subcmd, opts, identifier):
        """${cmd_name}: add a comment about a mirror 

        ${cmd_usage}
        ${cmd_option_list}
        """
        
        if not opts.comment:
            sys.exit('need to specify comment to add')

        mirror = lookup_mirror(self, identifier)
        mirror.comment = ' '.join([mirror.comment or '', '\n\n' + opts.comment])


    def do_enable(self, subcmd, opts, identifier):
        """${cmd_name}: enable a mirror 

        ${cmd_usage}
        ${cmd_option_list}
        """
        
        mirror = lookup_mirror(self, identifier)
        mirror.enabled = 1


    def do_disable(self, subcmd, opts, identifier):
        """${cmd_name}: disable a mirror

        ${cmd_usage}
        ${cmd_option_list}
        """
        
        mirror = lookup_mirror(self, identifier)
        mirror.statusBaseurl = 0
        mirror.enabled = 0


    def do_rename(self, subcmd, opts, identifier, new_identifier):
        """${cmd_name}: rename a mirror's identifier

        ${cmd_usage}
        ${cmd_option_list}
        """
        
        mirror = lookup_mirror(self, identifier)
        mirror.identifier = new_identifier


    @cmdln.option('--sql-debug', action='store_true',
                  help='Show SQL statements for debugging purposes.')
    @cmdln.option('-v', '--verbose', dest='verbosity', action='count', default=0,
                  help='Increase verbosity for debugging purposes. '
                       'Can be given multiple times.')
    @cmdln.option('-e', '--enable', action='store_true',
                  help='Enable a mirror, after it was scanned. Useful with -f')
    @cmdln.option('-a', '--all', action='store_true',
                  help='Scan all enabled mirrors.')
    @cmdln.option('-j', '--jobs', metavar='N',
                  help='Run up to N scanner queries in parallel.')
    @cmdln.option('-S', '--scanner', metavar='PATH',
                  help='Specify path to scanner.')
    @cmdln.option('-d', '--directory', metavar='DIR',
                  help='Scan only in dir under mirror\'s baseurl. '
                       'Default: start at baseurl. Does not delete files, only add.')
    def do_scan(self, subcmd, opts, *args):
        """${cmd_name}: scan mirrors

        Usage:
            mb scan [OPTS] IDENTIFIER [IDENTIFIER...]
        ${cmd_option_list}
        """
        from sqlobject.sqlbuilder import AND
        import mb.util
        import textwrap
        import mb.testmirror
        mb.testmirror.dont_use_proxies()

        mb.util.timer_start()

        cmd = []
        cmd.append(opts.scanner or '/usr/bin/scanner')

        if self.options.brain_instance:
            cmd.append('-b %s' % self.options.brain_instance)

        if opts.sql_debug:
            cmd.append('-S')
        for i in range(opts.verbosity):
            cmd.append('-v')

        if opts.enable:
            cmd.append('-e')
        if opts.directory:
            cmd.append('-d %s' % opts.directory)
        if opts.jobs:
            cmd += [ '-j', opts.jobs ]
        else:
            cmd.append('-f')

        cmd += [ '-I %s' % i for i in 
                 self.config.dbconfig.get('scan_top_include', '').split() ]
        cmd += [ '--exclude %s' % i for i in 
                 self.config.dbconfig.get('scan_exclude', '').split() ]
        cmd += [ '--exclude-rsync %s' % i for i in 
                 self.config.dbconfig.get('scan_exclude_rsync', '').split() ]

        if not opts.all and not args:
            sys.exit('No mirrors specified for scanning. Either give identifiers, or use -a [-j N].')

        mirrors = []
        if opts.all:
            mirrors = self.conn.Server.select(
                         AND(self.conn.Server.q.statusBaseurl, 
                             self.conn.Server.q.enabled))
        else:
            for arg in args:
                mirrors.append(lookup_mirror(self, arg))

        mirrors_to_scan = []
        mirrors_skipped = []
        if not opts.directory:
            mirrors_to_scan = [ i for i in mirrors ]
        else:
            print 'Checking for existance of %r directory' % opts.directory
            mirrors_have_file = mb.testmirror.mirrors_have_file(mirrors, opts.directory, url_type='scan')
            print
            for mirror in mirrors:
                for sample in mirrors_have_file:
                    if mirror.identifier == sample.identifier:
                        if sample.has_file:
                            if self.options.debug:
                                print '%s: scheduling scan.' % mirror.identifier
                            mirrors_to_scan.append(mirror)
                        else:
                            if self.options.debug:
                                print '%s: directory %s not found. Skipping.' % (mirror.identifier, opts.directory)
                            mirrors_skipped.append(mirror.identifier)

            if len(mirrors_to_scan):
                print 'Scheduling scan on:'
                print textwrap.fill(', '.join([ i.identifier for i in mirrors_to_scan ]),
                                    initial_indent='    ', subsequent_indent='  ')


        if not len(mirrors_to_scan):
            print 'No mirror to scan. Exiting.'
            sys.exit(0)

        cmd += [ mirror.identifier for mirror in mirrors_to_scan ]

        cmd = ' '.join(cmd)
        if self.options.debug:
            print cmd
        
        if opts.directory:
            print 'Completed in', mb.util.timer_elapsed()
            mb.util.timer_start()

        sys.stdout.flush()

        import os
        rc = os.system(cmd)

        if opts.enable and rc == 0:
            import time
            comment = ('*** scanned and enabled at %s.' % (time.ctime()))
            for mirror in mirrors_to_scan:
                mirror.comment = ' '.join([mirror.comment or '', '\n\n' + comment])

        sys.stdout.flush()
        if opts.directory and len(mirrors_skipped):
            print 'Skipped mirrors:'
            print textwrap.fill(', '.join(mirrors_skipped),
                                initial_indent='    ', subsequent_indent='  ')

        print 'Completed in', mb.util.timer_elapsed()


    def do_score(self, subcmd, opts, *args):
        """${cmd_name}: show or change the score of a mirror

        IDENTIFIER can be either the identifier or a substring.

        Usage:
            mb score IDENTIFIER [SCORE]
        ${cmd_option_list}
        """

        if len(args) == 1:
            identifier = args[0]
            score = None
        elif len(args) == 2:
            identifier = args[0]
            score = args[1]
        else:
            sys.exit('Wrong number of arguments.')
        
        mirror = lookup_mirror(self, identifier)

        if not score:
            print mirror.score
        else:
            print 'Changing score for %s: %s -> %s' \
                    % (mirror.identifier, mirror.score, score)
            mirror.score = int(score)
        

    @cmdln.option('-n', '--dry-run', action='store_true',
                  help='don\'t delete, but only show statistics.')
    def do_vacuum(self, subcmd, opts, *args):
        """${cmd_name}: clean up unreferenced files from the mirror database

        This should be done once a week for a busy file tree.
        Otherwise it should be rarely needed, but can possibly 
        improve performance if it is able to shrink the database.

        ${cmd_usage}
        ${cmd_option_list}
        """

        import mb.vacuum

        mb.vacuum.stale(self.conn)
        if not opts.dry_run:
            mb.vacuum.vacuum(self.conn)


    @cmdln.option('-u', '--url', action='store_true',
                        help='show the URL on the mirror')
    @cmdln.option('-p', '--probe', action='store_true',
                        help='probe the file')
    @cmdln.option('--md5', action='store_true',
                        help='show md5 hash of probed file')
    @cmdln.option('-m', '--mirror', 
                  help='apply operation to this mirror')
    def do_file(self, subcmd, opts, action, path):
        """${cmd_name}: operations on files: ls/rm/add

        ACTION is one of the following:

          ls PATH             list file
          rm PATH             remove PATH entry from the database
          add PATH            create database entry for file PATH

        PATH can contain * as wildcard, or alternatively % (SQL syntax).


        Examples:
          mb file ls /path/to/xorg-x11-libXfixes-7.4-1.14.i586.rpm
          mb file ls '*xorg-x11-libXfixes-7.4-1.14.i586.rpm'
          mb file add distribution/11.0/SHOULD_NOT_BE_VISIBLE -m cdn.novell.com
          mb file rm distribution/11.0/SHOULD_NOT_BE_VISIBLE -m MIRROR


        ${cmd_usage}
        ${cmd_option_list}
        """
        
        if path.startswith('/'):
            path = path[1:]

        import mb.files
        import mb.testmirror
        mb.testmirror.dont_use_proxies()

        if opts.md5:
            opts.probe = True

        if action in ['add', 'rm']:
            if not opts.mirror:
                sys.exit('this command needs to be used with -m')

        if opts.mirror:
            mirror = lookup_mirror(self, opts.mirror)
        else:
            mirror = None

        if action == 'ls':
            rows = mb.files.ls(self.conn, path)

            if opts.probe:
                samples = mb.testmirror.lookups_probe(rows, get_digest=opts.md5, get_content=False)
                print
            else:
                samples = []

            try:
                for row in rows:
                    if not row['identifier']:
                        # this is a stale entry, which will be vacuumed out
                        # next time the vacuumizer runs.
                        continue
                    print '%s %s %4d %s %s %-30s ' % \
                            (row['region'].lower(), row['country'].lower(),
                             row['score'], 
                             row['enabled'] == 1 and 'ok      ' or 'disabled',
                             row['status_baseurl'] == 1 and 'ok  ' or 'dead',
                             row['identifier']),
                    for sample in samples:
                        if row['identifier'] == sample.identifier:
                            if opts.probe:
                                print '%3s' % (sample.http_code or '   '),
                            if opts.probe and opts.md5:
                                print (sample.digest or ' ' * 32),
                    if opts.url:
                        print row['baseurl'] + row['path'],
                    print
            except KeyboardInterrupt:
                print >>sys.stderr, 'interrupted!'
                return 1


        elif action == 'add':
            mb.files.add(self.conn, path, mirror)

        elif action == 'rm':
            mb.files.rm(self.conn, path, mirror)

        else:
            sys.exit('ACTION must be either ls, rm or add.')


    @cmdln.option('-s', dest='segments', metavar='N', default=2,
                  help='show up to N distinct path segments.')
    @cmdln.option('-d', dest='dirpath', metavar='DIR',
                  help='list mirrors on which DIR was found.')
    def do_dirs(self, subcmd, opts, *args):
        """${cmd_name}: show directories that are in the database

        This subcommand is helpful when tuning scan excludes. You can
        list the directories of all paths that have ended up in the database,
        which is a good basis to define excludes, so the files can be eliminated
        from the database.

        Use -s N to show path components aggregated up to N segments.

        Use -d PATH to show mirrors which host directories that match PATH*.

        Examples for listing directories:
          mb dirs 
          mb dirs ftp.mirrorservice.org
          mb dirs -s 3
        Example for listing mirrors:
          mb dirs -d distribution/11.1-

        Usage:
            mb dirs [OPTS] [MIRROR]
        ${cmd_option_list}
        """
        
        import mb.files

        if args:
            mirror = lookup_mirror(self, args[0])
        else:
            mirror = None

        if opts.dirpath:
            for i in mb.files.dir_show_mirrors(self.conn, opts.dirpath):
                print i[0]
        else:
            for i in mb.files.dir_ls(self.conn, segments=opts.segments, mirror=mirror):
                print i[0]


    @cmdln.option('-c', '--caption', metavar='STRING',
                  help='insert this string as table caption')
    @cmdln.option('-t', '--title', metavar='STRING',
                  help='insert this string as title, meta-description and header')
    @cmdln.option('-B', '--html-footer', metavar='PATH',
                  help='include HTML (XHTML) from a file at the end')
    @cmdln.option('-H', '--html-header', metavar='PATH',
                  help='include HTML (XHTML) from a file at the top')
    @cmdln.option('-o', '--output', metavar='PATH',
                  help='write output to this file (tries to do this securely '
                  'and atomically)')
    @cmdln.option('-I', '--inline-images-from', metavar='PATH',
                  help='path to a directory with flag images to be inlined')
    @cmdln.option('-i', '--image-type', default='png', metavar='TYPE',
                  help='image file extension, e.g. png or gif')
    @cmdln.option('-f', '--format', default='txt', metavar='FORMAT',
                  help='output format of the mirrorlist, one of txt,txt2,xhtml')
    @cmdln.option('-s', '--skip-empty', action='store_true',
                  help="omit mirrors that don't have one of the marked files.")
    @cmdln.option('-F', '--filter', metavar='REGEX',
                  help='only markers matching this regular expression are used')
    @cmdln.option('-l', '--list-markers', action='store_true',
                  help='just show the defined marker files. (See "mb markers" '
                        'which also allows editing markers.)')
    def do_mirrorlist(self, subcmd, opts, *args):
        """${cmd_name}: generate a mirror list

        Generate a tabular mirror list, showing which subtree is
        listed on which mirrors.
        
        The presence of a particular subtree on the mirrors is determined
        by looking for "marker files", which are defined in the database for
        this purpose.


        Usage:
            mb mirrorlist [IDENTIFIER]
        ${cmd_option_list}
        """
        
        markers = self.conn.Marker.select()

        if opts.filter:
            import re
            p = re.compile(opts.filter)
            markers = [ i for i in markers if p.match(i.subtreeName) ]

        if opts.list_markers:
            for i in markers:
                print '%-20s    %r' % (i.subtreeName, i.markers.split())
            sys.exit(0)
        

        if args:
            import mb.conn
            mirrors = mb.conn.servers_match(self.conn.Server, args[0])
        else:
            from sqlobject.sqlbuilder import AND, NOT
            mirrors = self.conn.Server.select(AND(self.conn.Server.q.enabled,
                                                  NOT(self.conn.Server.q.prefixOnly),
                                                  NOT(self.conn.Server.q.asOnly),
                                                  self.conn.Server.q.country != '**'),
                                              orderBy=['region', 'country', '-score'])

        import mb.mirrorlists

        if opts.format not in mb.mirrorlists.supported:
            sys.exit('format %r not supported' % opts.format)

        mb.mirrorlists.genlist(conn=self.conn, opts=opts, mirrors=mirrors, markers=markers, format=opts.format)



    @cmdln.option('--project', metavar='PROJECT',
                  help='Specify a project name (previously corresponding to a MirrorBrain instance).')
    @cmdln.option('--commit', metavar='VCS',
                  help='run "VCS commit" on the directory specified via --target-dir')
    @cmdln.option('--target-dir', metavar='PATH',
                  help='For the "vcs" output format, specify a target directory to place files into')
    @cmdln.option('--format', metavar='FORMAT',
            help='Specify the output format: [django|postgresql|vcs]')
    def do_export(self, subcmd, opts, *args):
        """${cmd_name}: export the mirror list as text file

        There are different output formats:

        Format "django" is suitable to be used in a Django ORM.

        Format "postgresql" is suitable to be imported into a PostgreSQL
        database.

        Format "vcs" generates a file tree which can be imported/committed into
        a version control system (VCS). This can be used to periodically dump
        the database into a working copy of such a repository and commit the
        changes, making use of the commit mail mechanism of the VCS to send
        change notifications.
        You need to specify --target-dir=PATH for this. 
        If you use the --commit=VCS option, "VCS commit" will be run after the
        export on the directory.

        Example:
          mb export --format vcs --target-dir ~/svn/mirrors-export --commit=svn
        (you could run it hourly by cron)

        ${cmd_usage}
        ${cmd_option_list}
        """

        import mb.exports

        if not opts.format:
            sys.exit('You need to specify an output format. See --help output.')

        if opts.format == 'django' and not opts.project:
            sys.exit('For Django ORM format, specify a project name (roughly corresponding to a MirrorBrain instance) name with --project')
        if opts.format == 'vcs' and not opts.target_dir:
            sys.exit('To export for a version control system, specify a target directory')

        if opts.format == 'django':
            print mb.exports.django_header

            # FIXME: add new fields: operator_name, operator_url, public_notes
            print """Project(name='%s').save()""" % opts.project

        elif opts.format == 'postgresql':
            print mb.exports.postgresql_header

        elif opts.format == 'vcs':
            import os, os.path
            if not os.path.exists(opts.target_dir):
                os.makedirs(opts.target_dir, 0750)
            os.chdir(opts.target_dir)
            for i in os.listdir('.'):
                if i.startswith('.'): continue
                os.unlink(i)

        else:
            sys.exit('unknown format %r' % opts.format)


        mirrors = self.conn.Server.select()
        for m in mirrors:
            if m.comment == None:
                #print 'null comment', m
                m.comment = ''
            d = mb.conn.server2dict(m)
            d.update(dict(project=opts.project))

            #print >>sys.stderr, d

            # replace None's
            #for i in mb.conn.server_editable_attrs:
            for i in ['asn', 'prefix', 'asOnly', 'prefixOnly', 'lat', 'lng', 'scanFpm']:
                if d[i] == None: d[i] = '0'
            for i in ['prefix', 'baseurlRsync', 'admin', 'adminEmail']:
                if d[i] == None: d[i] = ''

            if opts.format == 'django':
                print mb.exports.django_template % d

            elif opts.format == 'postgresql':
                print mb.exports.postgresql_template % d

            elif opts.format == 'vcs':
                s = mb.conn.server_show_template % mb.conn.server2dict(m)
                s = '\n'.join([ i for i in s.splitlines() if not i.startswith('statusBaseurl') ]) + '\n'
                open(m.identifier, 'w').write(s)

        if opts.format == 'vcs' and opts.commit:
            import commands
            lines = commands.getoutput('%s status' % opts.commit).splitlines()
            for line in lines:
                state, i = line.split()
                if state == '!':
                    os.system('%s delete %s > /dev/null' % (opts.commit, i))
                elif state == '?':
                    os.system('%s add %s > /dev/null' % (opts.commit, i))

            os.system('%s commit -m "autocommit by mb" %s > /dev/null' \
                        % (opts.commit, opts.target_dir))


if __name__ == '__main__':
    import sys
    mirrordoctor = MirrorDoctor()
    sys.exit( mirrordoctor.main() )

