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

__version__ = '2.17.0'
__author__ = 'Peter Poeml <poeml@cmdline.net>'
__copyright__ = 'Novell / SUSE Linux Products GmbH'
__license__ = 'GPL'
__url__ = 'http://mirrorbrain.org'



import cmdln
import mb.geoip
import mb.mberr
import signal


def catchterm(*args):
    raise mb.mberr.SignalInterrupt

for name in 'SIGBREAK', 'SIGHUP', 'SIGTERM':
    num = getattr(signal, name, None)
    if num: signal.signal(num, catchterm)



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
        optparser.add_option('--config', dest="configpath", metavar="CONFIGPATH",
                             default='/etc/mirrorbrain.conf',
                             help='location of configuration file '
                                  '(default: /etc/mirrorbrain.conf)')
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
        self.config = mb.conf.Config(conffile = self.options.configpath, instance = self.options.brain_instance)

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


    @cmdln.option('--prefix-only', action='store_true',
                        help='set the mirror to handle only its network prefix')
    @cmdln.option('--as-only', action='store_true',
                        help='set the mirror to handle only its autonomous system')
    @cmdln.option('--country-only', action='store_true',
                        help='set the mirror to handle only its country')
    @cmdln.option('--region-only', action='store_true',
                        help='set the mirror to handle only its region')
    @cmdln.option('-C', '--comment', metavar='ARG',
                        help='comment string')

    @cmdln.option('--operator-name', metavar='ARG',
                        help='name of the organization operating the mirror')
    @cmdln.option('--operator-url', metavar='ARG',
                        help='URL of the organization operating the mirror')
    @cmdln.option('-a', '--admin', metavar='ARG',
                        help='admins\'s name')
    @cmdln.option('-e', '--admin-email', metavar='ARG',
                        help='admins\'s email address')

    @cmdln.option('-s', '--score', default=100, metavar='ARG',
                        help='priority of this mirror, defaults to 100 (depreciated. Use --prio)')
    @cmdln.option('-p', '--prio', default=100, metavar='ARG',
                        help='priority of this mirror, defaults to 100')

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


        try:
            # does an existing mirror have the same identifier? They must be unique.
            m = self.conn.Server.select(self.conn.Server.q.identifier == identifier)[0]
        except IndexError:
            pass
        else:
            sys.exit('Error: a mirror\'s identifier must be unique.\n'
                     'There is already a mirror using this identifier. See output of `mb show %s`.\n'
                     'Exiting. ' % identifier)

        if not opts.http:
            sys.exit('An HTTP base URL needs to be specified')

        scheme, host, path, a, b, c = urlparse.urlparse(opts.http)
        if ':' in host:
            host, port = host.split(':')
        if not opts.region:
            opts.region = mb.geoip.lookup_region_code(host)
        if not opts.country:
            opts.country = mb.geoip.lookup_country_code(host)
        lat, lng = mb.geoip.lookup_coordinates(host)

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
                             lat          = lat or 0,
                             lng          = lng or 0,
                             score        = opts.score,
                             enabled      = 0,
                             statusBaseurl = 0,
                             admin        = opts.admin or '',
                             adminEmail   = opts.admin_email or '',
                             operatorName = opts.operator_name or '',
                             operatorUrl  = opts.operator_url or '',
                             otherCountries = '',
                             publicNotes  = '',
                             comment      = opts.comment \
                               or 'Added - %s' % time.ctime(),
                             scanFpm      = 0,
                             countryOnly  = opts.country_only or 0,
                             regionOnly   = opts.region_only or 0,
                             asOnly       = opts.as_only or 0,
                             prefixOnly   = opts.prefix_only or 0)
        if self.options.debug:
            print s


    @cmdln.option('--number-of-files', '-N', action='store_true',
                        help='display number of files the mirror is known to have')
    @cmdln.option('--prefix-only', action='store_true',
                        help='display whether the mirror is configured to handle only its network prefix')
    @cmdln.option('--as-only', action='store_true',
                        help='display whether the mirror is configured to handle only its autonomous system')
    @cmdln.option('--country-only', action='store_true',
                        help='display whether the mirror is configured to handle only its country')
    @cmdln.option('--region-only', action='store_true',
                        help='display whether the mirror is configured to handle only its region')
    @cmdln.option('--other-countries', action='store_true',
                        help='also display other countries that '
                             'a mirror is configured to handle')
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
    @cmdln.option('-R', '--rsync-url', action='store_true',
                        help='also display the rsync URL')
    @cmdln.option('-F', '--ftp-url', action='store_true',
                        help='also display the FTP URL')
    @cmdln.option('-H', '--http-url', action='store_true',
                        help='also display the HTTP URL')
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
            import mb.conn
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
            if opts.other_countries:
                s.append('%2s' % mirror.otherCountries)
            if opts.asn:
                s.append('%5s' % mirror.asn)
            if opts.prefix:
                s.append('%-19s' % mirror.prefix)
            if opts.http_url:
                s.append('%-55s' % mirror.baseurl)
            if opts.ftp_url:
                s.append('%-55s' % mirror.baseurlFtp)
            if opts.rsync_url:
                s.append('%-55s' % mirror.baseurlRsync)
            # boolean flags
            if opts.region_only:
                s.append('region_only=%s' % mirror.regionOnly)
            if opts.country_only:
                s.append('country_only=%s' % mirror.countryOnly)
            if opts.as_only:
                s.append('as_only=%s' % mirror.asOnly)
            if opts.prefix_only:
                s.append('prefix_only=%s' % mirror.prefixOnly)
            if opts.number_of_files:
                import mb.core
                s.append('nfiles=%s' % mb.core.mirror_get_nfiles(self.conn, mirror))
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
            print '%s (AS%s) %s' % (r.prefix, r.asn, r.ip6)
        if opts.all_prefixes:
            r2 = mb.asn.asn_prefixes(self.conn, r.asn)
            print ', '.join(r2)


    @cmdln.option('--all-mirrors', action='store_true',
                        help='update *all* mirrors (also disabled ones)')
    @cmdln.option('-A', '--all', action='store_true',
                        help='update all metadata (same as "-c -a -p --country --region")')
    @cmdln.option('--region', action='store_true',
                        help='update the region setting with a fresh GeoIP lookup')
    @cmdln.option('--country', action='store_true',
                        help='update the country setting with a fresh GeoIP lookup')
    @cmdln.option('-p', '--prefix', action='store_true',
                        help='update the network prefix')
    @cmdln.option('-a', '--asn', action='store_true',
                        help='update the AS number')
    @cmdln.option('-c', '--coordinates', action='store_true',
                        help='update the geographical coordinates')
    @cmdln.option('-n', '--dry-run', action='store_true',
                        help='don\'t actually do anything, just show what would be done')
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

        if opts.all:
            opts.asn = opts.prefix = opts.coordinates = opts.country = opts.region = True

        if not (opts.asn or opts.prefix or opts.coordinates or opts.country or opts.region):
            sys.exit('At least one of -c, -a, -p, --country, --region must be given as option.')

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
            hostname = hostname_from_url(mirror.baseurl)

            #if opts.prefix or opts.asn:
            try:
                res = iplookup(self.conn, hostname)
            except mb.mberr.NameOrServiceNotKnown, e:
                print '%s:' % mirror.identifier, e.msg
                #print '%s: without DNS lookup, no further lookups are possible' % mirror.identifier
                continue


            if res:
                if mirror.ipv6Only != res.ipv6Only():
                    print '%s: updating ipv6Only flag (%s -> %s)' \
                        % (mirror.identifier, mirror.ipv6Only, res.ipv6Only())
                    if not opts.dry_run:
                        mirror.ipv6Only = res.ipv6Only()

            if opts.prefix and res:
                if mirror.prefix != res.prefix:
                    print '%s: updating network prefix (%s -> %s)' \
                        % (mirror.identifier, mirror.prefix, res.prefix)
                    if not opts.dry_run:
                        mirror.prefix = res.prefix
            if opts.asn and res:
                if mirror.asn != res.asn:
                    print '%s: updating autonomous system number (%s -> %s)' \
                        % (mirror.identifier, mirror.asn, res.asn)
                    if not opts.dry_run:
                        mirror.asn = res.asn

            if opts.coordinates:
                lat, lng = mb.geoip.lookup_coordinates(hostname)
                if float(mirror.lat or 0) != lat or float(mirror.lng or 0) != lng:
                    print '%s: updating geographical coordinates (%s %s -> %s %s)' \
                        % (mirror.identifier, mirror.lat, mirror.lng, lat, lng)
                    if not opts.dry_run:
                        mirror.lat, mirror.lng = lat, lng

            if opts.region:
                region = mb.geoip.lookup_region_code(hostname)
                if mirror.region != region:
                    print '%s: updating region (%s -> %s)' \
                        % (mirror.identifier, mirror.region, region)
                    if not opts.dry_run:
                        mirror.region = region

            if opts.country:
                country = mb.geoip.lookup_country_code(hostname)
                if mirror.country != country:
                    print '%s: updating country (%s -> %s)' \
                        % (mirror.identifier, mirror.country, country)
                    if not opts.dry_run:
                        mirror.country = country



    def do_test(self, subcmd, opts, identifier):
        """${cmd_name}: test if a mirror is working

        This does only rudimentary checking for now. It only does a request
        on the base URL. But more checks could easily be added.
        (See the implementation of do_probefile() for ideas.)

        ${cmd_usage}
        ${cmd_option_list}
        """

        mirror = lookup_mirror(self, identifier)
        import mb.testmirror
        r = mb.testmirror.access_http(mirror.identifier, mirror.baseurl)
        print r
        print 'content: %r...' % r.content[:240]


    @cmdln.option('--content', action='store_true',
                        help='download and show the content')
    @cmdln.option('--md5', action='store_true',
                        help='download and show the md5 sum')
    @cmdln.option('--urls', dest='url_type', metavar='TYPE', default='scan',
                        help='type of URLs to be probed (scan|http|all). Default: scan.')
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
        """${cmd_name}: edit a new mirror entry in $EDITOR/$VISUAL

        Usage:
            mb edit IDENTIFIER
        ${cmd_option_list}
        """
        mirror = lookup_mirror(self, identifier)
        
        import mb.conn
        old_dict = mb.conn.server2dict(mirror)
        old = mb.conn.server_show_template % old_dict

        import mb.util
        boilerplate = """#
# Note: - You cannot modify 'identifier' or 'id'. You can use 'mb rename' though.
#       - AS, prefix, lat and lng should be modified through 'mb update' 
#         ('mb update -A –all-mirrors' for all).
#
"""
        new = mb.util.edit_file(old, boilerplate=boilerplate)
        if not new:
            print 'Quitting.'
        else:
            new_dict = mb.conn.servertext2dict(new)

            for i in mb.conn.server_editable_attrs:
                if not old_dict[i] and not new_dict[i]:
                    continue

                if ( old_dict[i] and not new_dict[i] ) or \
                   ( str(old_dict[i]) != new_dict[i] ):

                    if not new_dict[i]:
                        print 'unsetting %s (was: %r)' % (i, old_dict[i])
                    else:
                        print 'changing %s from %r to %r' % (i, old_dict[i], new_dict[i])

                    a = new_dict[i]
                    if a == 'False': a = False
                    if a == 'True': a = True
                    if a == None: a = ''
                    if type(getattr(mirror, i)) in [type(1L), type(1), bool]:
                        try:
                            a = int(a)
                        except ValueError:
                            a = 0
                    setattr(mirror, i, a)
                #else:
                #    print 'unchanged: %s' % i



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
    @cmdln.option('-q', '--quiet', dest='quietness', action='count', default=0,
                  help='Produce less output. '
                       'Can be given multiple times.')
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

        if self.options.configpath:
            cmd.append('--config %s' % self.options.configpath)
        if self.options.brain_instance:
            cmd.append('-b %s' % self.options.brain_instance)

        if opts.sql_debug:
            cmd.append('-S')
        for i in range(opts.verbosity):
            cmd.append('-v')
        for i in range(opts.quietness):
            cmd.append('-q')

        if opts.enable:
            cmd.append('-e')
        if opts.directory:
            cmd.append('-d %s' % opts.directory)
        if opts.jobs:
            cmd += [ '-j', opts.jobs ]
        if opts.enable or args:
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
        if not opts.directory or len(mirrors) == 1:
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
        
        if opts.directory and len(mirrors) != 1:
            print 'Completed in', mb.util.timer_elapsed()
            mb.util.timer_start()

        sys.stdout.flush()

        import os
        rc = os.system(cmd)

        if opts.enable and rc == 0:
            import time
            import mb.testmirror
            tt = time.ctime()
            comment = ('*** scanned and enabled at %s.' % tt)
            for mirror in mirrors_to_scan:
                mirror.comment = ' '.join([mirror.comment or '', '\n\n' + comment])

                print '%s %s: testing status of base URL...' % (tt, mirror.identifier)
                t = mb.testmirror.access_http(mirror.identifier, mirror.baseurl)
                if t.http_code == 200:
                    mirror.statusBaseurl = 1
                    mirror.enabled = 1
                    print '%s %s: OK. Mirror is online now.' % (tt, mirror.identifier)
                else:
                    print '%s %s: Error: base URL does not work: %s' \
                            % (tt, mirror.identifier, mirror.baseurl)

        sys.stdout.flush()
        if opts.directory and len(mirrors_skipped):
            print 'Skipped mirrors:'
            print textwrap.fill(', '.join(mirrors_skipped),
                                initial_indent='    ', subsequent_indent='  ')

        if opts.quietness < 2:
            print 'Completed in', mb.util.timer_elapsed()



    @cmdln.option('--force', action='store_true',
                        help='force refreshing all cached hashes')
    @cmdln.option('-n', '--dry-run', action='store_true',
                        help='don\'t actually do anything, just show what would be done')
    @cmdln.option('--copy-permissions', action='store_true',
                        help='copy the permissions of directories and files '
                             'to the hashes files. Normally, this should not '
                             'be needed, because the hash files don\'t contain '
                             'any reversible information.')
    @cmdln.option('-f', '--file-mask', metavar='REGEX',
                        help='regular expression to select files to create hashes for')
    @cmdln.option('-i', '--ignore-mask', metavar='REGEX',
                        help='regular expression to ignore certain files or directories. '
                             'If matching a file, no hashes are created for it. '
                             'If matching a directory, the directory is ignored and '
                             'deleted in the target tree.')
    @cmdln.option('-b', '--base-dir', metavar='PATH',
                        help='set the base directory (so that you can work on a '
                             'subdirectory -- see examples)')
    @cmdln.option('-t', '--target-dir', metavar='PATH',
                        help='set the target directory (required)')
    @cmdln.option('-v', '--verbose', action='store_true',
                        help='show more information')
    def do_makehashes(self, subcmd, opts, startdir):
        """${cmd_name}: Create or update verification hashes, e.g. for
        inclusion into Metalinks and Torrents, or to be requested by appending
        .md5 or .sha1 to an URL.

        Simplest Examples:
            mb makehashes -t /srv/metalink-hashes/srv/ooo /srv/ooo

        This is aequivalent:
            mb makehashes -t /srv/metalink-hashes/srv/ooo -b /srv/ooo /srv/ooo

        Hash only the subdirectory extended/iso/de:
            mb makehashes -t /srv/metalink-hashes/srv/ooo -b /srv/ooo /srv/ooo/extended/iso/de
        
        Further examples:
            mb makehashes \\
            -t /srv/metalink-hashes/srv/ftp/pub/opensuse/repositories/home:/poeml \\
            /srv/ftp-stage/pub/opensuse/repositories/home:/poeml \\
            -i '^.*/repoview/.*$'
            mb makehashes \\
            -f '.*.(torrent|iso)$' \\
            -t /var/lib/apache2/metalink-hashes/srv/ftp/pub/opensuse \\
            -b /srv/ftp-stage/pub/opensuse \\
            /srv/ftp-stage/pub/opensuse/distribution/11.0/iso \\
            -n

        ${cmd_usage}
        ${cmd_option_list}
        """

        import os
        import fcntl
        import errno
        import re
        import shutil
        import mb.hashes
        import mb.files

        if not opts.target_dir:
            sys.exit('You must specify the target directory (-t)')
        if not opts.base_dir:
            opts.base_dir = startdir
            #sys.exit('You must specify the base directory (-b)')

        if not opts.target_dir.startswith('/'):
            sys.exit('The target directory must be an absolut path')
        if not opts.base_dir.startswith('/'):
            sys.exit('The base directory must be an absolut path')

        startdir = startdir.rstrip('/')
        opts.target_dir = opts.target_dir.rstrip('/')
        opts.base_dir = opts.base_dir.rstrip('/')

        double_slashes = re.compile('/+')
        startdir = re.sub(double_slashes, '/', startdir)
        opts.target_dir = re.sub(double_slashes, '/', opts.target_dir)
        opts.base_dir = re.sub(double_slashes, '/', opts.base_dir)

        if not os.path.exists(startdir):
            sys.exit('STARTDIR %r does not exist' % startdir) 

        directories_todo = [startdir]

        if opts.ignore_mask: 
            opts.ignore_mask = re.compile(opts.ignore_mask)
        if opts.file_mask: 
            opts.file_mask = re.compile(opts.file_mask)

        unlinked_files = unlinked_dirs = 0

        while len(directories_todo) > 0:
            src_dir = directories_todo.pop(0)

            try:
                src_dir_mode = os.stat(src_dir).st_mode
            except OSError, e:
                if e.errno == errno.ENOENT:
                    sys.stderr.write('Directory vanished: %r\n' % src_dir)
                    continue

            dst_dir = os.path.join(opts.target_dir, src_dir[len(opts.base_dir):].lstrip('/'))
            dst_dir_db = src_dir[len(opts.base_dir):].lstrip('/')
            #print dst_dir_db

            if not opts.dry_run:
                if not os.path.isdir(dst_dir):
                    os.makedirs(dst_dir, mode = 0755)
                if opts.copy_permissions:
                    os.chmod(dst_dir, src_dir_mode)
                else:
                    os.chmod(dst_dir, 0755)

            try:
                dst_names = os.listdir(dst_dir)
                dst_names.sort()
                dst_names_db = [ (os.path.basename(i), j) 
                                 for i, j in mb.files.dir_filelist(self.conn, dst_dir_db)]
                dst_names_db_dict = dict(dst_names_db)
                dst_names_db_keys = dst_names_db_dict.keys()
                #print dst_names_db_keys
            except OSError, e:
                if e.errno == errno.ENOENT:
                    sys.exit('\nSorry, cannot really continue in dry-run mode, because directory %r does not exist.\n'
                             'You might want to create it:\n'
                             '  mkdir %s' % (dst_dir, dst_dir))


            # a set offers the fastest access for "foo in ..." lookups
            try:
                src_basenames = set(os.listdir(src_dir))
            except os.error:
                sys.stderr.write('Cannot access directory: %r\n' % src_dir)
                src_basenames = []

            if opts.verbose:
                print 'Examining directory', src_dir

            dst_keep_db = set()
            dst_keep = set()
            dst_keep.add('LOCK')

            # FIXME: given that we don't need -t parameter anymore... can we create a lock hierarchy in /tmp instead??
            lockfile = os.path.join(dst_dir, 'LOCK')
            try:
                if not opts.dry_run:
                    lock = open(lockfile, 'w')
                    fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    try:
                        os.stat(lockfile)
                    except OSError, e: 
                        if e.errno == errno.ENOENT:
                            if opts.verbose:
                                print '====== skipping %s, which we were about to lock' % lockfile
                            continue

                if opts.verbose:
                    print 'locked %s' % lockfile
            except IOError, e:
                if e.errno in [ errno.EAGAIN, errno.EACCES, errno.EWOULDBLOCK ]:
                    print 'Skipping %r, which is locked' % src_dir
                    continue
                else:
                    raise


            for src_basename in sorted(src_basenames):
                src = os.path.join(src_dir, src_basename)

                if opts.ignore_mask and re.match(opts.ignore_mask, src):
                    continue

                # stat only once
                try:
                    hasheable = mb.hashes.Hasheable(src_basename, 
                                                    src_dir=src_dir, 
                                                    dst_dir=dst_dir,
                                                    base_dir=opts.base_dir,
                                                    do_zsync_hashes=self.config.dbconfig.get('zsync_hashes'),
                                                    do_chunked_hashes=self.config.dbconfig.get('chunked_hashes'),
                                                    chunk_size=self.config.dbconfig.get('chunk_size'))
                except OSError, e:
                    if e.errno == errno.ENOENT:
                        sys.stderr.write('File vanished: %r\n' % src)
                        continue

                if hasheable.islink():
                    if opts.verbose:
                        print 'ignoring link', src
                    continue

                elif hasheable.isreg():
                    if not opts.file_mask or re.match(opts.file_mask, src_basename):
                        #if opts.verbose:
                        #    print 'dst:', dst
                        hasheable.check_file(verbose=opts.verbose, 
                                            dry_run=opts.dry_run, 
                                            force=opts.force, 
                                            copy_permissions=opts.copy_permissions)
                        hasheable.check_db(conn=self.conn,
                                           verbose=opts.verbose, 
                                           dry_run=opts.dry_run,
                                           force=opts.force)
                        dst_keep.add(hasheable.dst_basename)
                        dst_keep_db.add(hasheable.basename)

                elif hasheable.isdir():
                    directories_todo.append(src)  # It's a directory, store it.
                    dst_keep.add(hasheable.basename)
                    dst_keep_db.add(hasheable.basename)


            dst_remove = set(dst_names) - dst_keep
            #print 'old', dst_remove
            dst_remove_db = set(dst_names_db_keys) - dst_keep_db
            #print 'new', dst_remove_db

            # print 'files to keep:'
            # print dst_keep
            # print
            # print 'files to remove:'
            # print dst_remove
            # print

            for i in sorted(dst_remove):
                i_path = os.path.join(dst_dir, i)
                #print i_path

                if (opts.ignore_mask and re.match(opts.ignore_mask, i_path)):
                    print 'ignoring, not removing %s', i_path
                    continue

                if os.path.isdir(i_path):
                    print 'Recursively removing obsolete directory %r' % i_path
                    if not opts.dry_run: 
                        try:
                            shutil.rmtree(i_path)
                        except OSError, e:
                            if e.errno == errno.EACCES:
                                sys.stderr.write('Recursive removing failed for %r (%s). Ignoring.\n' \
                                                    % (i_path, os.strerror(e.errno)))
                            else:
                                sys.exit('Recursive removing failed for %r: %s\n' \
                                                    % (i_path, os.strerror(e.errno)))

                        relpath = os.path.join(dst_dir_db, i)
                        print 'Recursively removing hashes in database: %s/*' % relpath
                        mb.files.hashes_dir_delete(self.conn, relpath)

                    unlinked_dirs += 1
                    
                else:
                    print 'Unlinking obsolete %r' % i_path
                    if not opts.dry_run: 
                        try:
                            os.unlink(i_path)
                        except OSError, e:
                            if e.errno != errno.ENOENT:
                                sys.stderr.write('Unlink failed for %r: %s\n' \
                                                    % (i_path, os.strerror(e.errno)))
                    unlinked_files += 1
            ids_to_delete = []
            for i in sorted(dst_remove_db):
                relpath = os.path.join(dst_dir_db, i)
                dbid = dst_names_db_dict.get(i)
                if dbid:
                    print 'Obsolete hash in db: %r (id %s)' % (relpath, dbid)
                    ids_to_delete.append(dbid)
                else:
                    pass # not in the hash table

            if len(ids_to_delete):
                print 'Deleting %s obsolete hashes from hash table' % len(ids_to_delete)
                if not opts.dry_run:
                    mb.files.hashes_list_delete(self.conn, ids_to_delete)

            if opts.verbose:
                print 'unlocking', lockfile 
            if not opts.dry_run:
                os.unlink(lockfile)
                lock.close()

        if  unlinked_files or unlinked_dirs:
            print 'Unlinked %s files, %d directories.' % (unlinked_files, unlinked_dirs)




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
        

    # the previous command name
    @cmdln.alias('vacuum')

    @cmdln.option('-n', '--dry-run', action='store_true',
                  help='don\'t delete, but only show statistics.')
    @cmdln.option('-q', '--quiet', dest='quietness', action='count', default=0,
                  help='Produce less output. '
                       'Can be given multiple times.')
    def do_db(self, subcmd, opts, *args):
        """${cmd_name}: perform database maintenance
        
        This command needs to be called with one of the following actions:
        
        vacuum
          Clean up unreferenced
          files from the mirror database.
          This should be done once a week for a busy file tree.  Otherwise it
          should be rarely needed, but can possibly improve performance if it
          is able to shrink the database.

          When called with the -n option, only the number of files to be
          cleaned up is printed. This is purely for information.

        sizes
          Print the size of each database relation. This can provide insight
          for the most appropriate database tuning.

        shell
          Conveniently open a database shell.


        usage:
            mb db vacuum [-q] [-n]
            mb db sizes
            mb db shell
        ${cmd_option_list}
        """

        import mb.dbmaint

        # this subcommand was renamed from "mb vacuum" to "mb db <action>"
        # let's keep the old way working
        if subcmd == 'vacuum':
                mb.dbmaint.stale(self.conn, opts.quietness)
                mb.dbmaint.vacuum(self.conn, opts.quietness)
                sys.exit(0)

        if len(args) < 1:
            sys.exit('Too few arguments.')
        action = args[0]

        if action == 'sizes':
            mb.dbmaint.stats(self.conn)
        elif action == 'vacuum':
            if not opts.dry_run:
                mb.dbmaint.stale(self.conn, opts.quietness)
                mb.dbmaint.vacuum(self.conn, opts.quietness)
            else:
                mb.dbmaint.stale(self.conn, opts.quietness)
        elif action == 'shell':
            mb.dbmaint.shell(self.config.dbconfig)

        else:
            sys.exit('unknown action %r' % action)






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
                        print row['baseurl'].rstrip('/') + '/' + row['path'],
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
    @cmdln.option('--missing', dest='missingdirpath', metavar='DIR',
                  help='list mirrors on which DIR was *not* found.')
    @cmdln.option('--sql-debug', action='store_true',
                  help='Show SQL statements for debugging purposes.')
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
        elif opts.missingdirpath:
            for i in mb.files.dir_show_mirrors(self.conn, opts.missingdirpath, missing=True):
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
                                              orderBy=['region', 'country', 'operatorName'])

        import mb.mirrorlists

        if opts.format not in mb.mirrorlists.supported:
            sys.exit('format %r not supported' % opts.format)

        mb.mirrorlists.genlist(conn=self.conn, opts=opts, mirrors=mirrors, markers=markers, format=opts.format)



    @cmdln.option('--project', metavar='PROJECT',
                  help='(only for django format) Specify a project name (previously corresponding to a MirrorBrain instance).')
    @cmdln.option('--commit', metavar='VCS',
                  help='run "VCS commit" on the directory specified via --target-dir')
    @cmdln.option('--target-dir', metavar='PATH',
                  help='For the "vcs" output format, specify a target directory to place files into')
    @cmdln.option('--format', metavar='FORMAT',
            help='Specify the output format: [django|postgresql|mirmon|mirmon-apache|vcs]')
    def do_export(self, subcmd, opts, *args):
        """${cmd_name}: export the mirror list as text file

        There are different output formats:

        Format "django" is suitable to be used in a Django ORM.

        Format "postgresql" is suitable to be imported into a PostgreSQL
        database.

        Format "mirmon" creates a list of mirrors to be included in a mirmon
        configuration. "mirmon-apache" uses a different format, used when
        mirmon is configured with its option "list_style = apache".

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

        elif opts.format in ['mirmon', 'mirmon-apache']:
            pass

        elif opts.format == 'vcs':
            import os, os.path
            if not os.path.exists(opts.target_dir):
                os.makedirs(opts.target_dir, 0750)
            os.chdir(opts.target_dir)
            if not os.path.isdir('.svn'):
                sys.exit('%r doesn\'t seem to be a Subversion working copy' % opts.target_dir)
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

            elif opts.format in ['mirmon', 'mirmon-apache']:
                if opts.format == 'mirmon':
                    mirmon_template = mb.exports.mirmon_template
                elif opts.format == 'mirmon-apache':
                    mirmon_template = mb.exports.mirmon_apache_template

                if not m.enabled:
                    continue
                for proto, urlname in [('http', 'baseurl'), 
                                       ('ftp', 'baseurlFtp'),
                                       ('rsync', 'baseurlRsync')]:
                    if d[urlname]:
                        print mirmon_template \
                                % dict(proto=proto, 
                                       url=d[urlname], 
                                       adminEmail=d['adminEmail'],
                                       country=d['country'])

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
    try:
        r = mirrordoctor.main()

    except mb.mberr.SignalInterrupt:
        print >>sys.stderr, 'killed!'
        r = 1

    except KeyboardInterrupt:
        print >>sys.stderr, 'interrupted!'
        r = 1

    except mb.mberr.UserAbort:
        print >>sys.stderr, 'aborted.'
        r = 1

    except (mb.mberr.ConfigError, 
            mb.mberr.NoConfigfile,
            mb.mberr.MirrorNotFoundError,
            mb.mberr.SocketError), e:
        print >>sys.stderr, e.msg
        r = 1

    sys.exit(r)

