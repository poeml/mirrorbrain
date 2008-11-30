#!/usr/bin/python

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
        #optparser = cmdln.CmdlnOptionParser(self, version=get_version())
        optparser = cmdln.CmdlnOptionParser(self)
        optparser.add_option('-d', '--debug', action='store_true',
                             help='print info useful for debugging')
        optparser.add_option('-b', '--brain-instance', 
                             help='the mirrorbrain instance to use. '
                                  'Corresponds to a section in '
                                  '/etc/mirrorbrain.conf which is named the same.')
        return optparser


    def postoptparse(self):
        """runs after parsing global options"""

        import mb.conf
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

        import urlparse


        if not opts.http:
            sys.exit('An HTTP base URL needs to be specified')

        scheme, host, path, a, b, c = urlparse.urlparse(opts.http)
        if not opts.region:
            opts.region = mb.geoip.lookup_region_code(host)
        if not opts.country:
            opts.country = mb.geoip.lookup_country_code(host)

        if opts.region == '--' or opts.country == '--':
            raise ValueError('region lookup failed')

        s = self.conn.Server(identifier   = identifier,
                             baseurl      = opts.http,
                             baseurlFtp   = opts.ftp or '',
                             baseurlRsync = opts.rsync,
                             region       = opts.region,
                             country      = opts.country,
                             score        = opts.score,
                             enabled      = 0,
                             statusBaseurl = 0,
                             admin        = opts.admin,
                             adminEmail   = opts.admin_email,
                             comment      = opts.comment)
        if self.options.debug:
            print s


    #@cmdln.option('-s', '--show-score', action='store_true',
    #                    help='show score of each mirror')
    @cmdln.option('--disabled', action='store_true',
                        help='show only disabled mirrors')
    @cmdln.option('-a', '--show-disabled', action='store_true',
                        help='do not hide disabled mirrors')
    @cmdln.option('-c', '--country', metavar='XY',
                        help='show only mirrors whose country matches XY')
    @cmdln.option('-r', '--region', metavar='XY',
                        help='show only mirrors whose region matches XY '
                        '(possible values: sa,na,oc,af,as,eu)')
    def do_list(self, subcmd, opts, *args):
        """${cmd_name}: list mirrors

        Usage:
            mirrordoctor list [IDENTIFIER]
        ${cmd_option_list}
        """
        from sqlobject.sqlbuilder import LIKE
        if opts.country:
            mirrors = self.conn.Server.select("""country LIKE '%%%s%%'""" % opts.country)
        elif opts.region:
            mirrors = self.conn.Server.select("""region LIKE '%%%s%%'""" % opts.region)
        elif args:
            mirrors = mb.conn.servers_match(self.conn.Server, args[0])
        else:
            mirrors = self.conn.Server.select()

        for mirror in mirrors:
            if opts.show_disabled:
                print mirror.identifier #, mirror.score
            elif opts.disabled:
                if not mirror.enabled:
                    print mirror.identifier #, mirror.score
            else:
                if mirror.enabled:
                    print mirror.identifier #, mirror.score


    def do_show(self, subcmd, opts, identifier):
        """${cmd_name}: show a mirror entry

        ${cmd_usage}
        ${cmd_option_list}
        """

        mirror = lookup_mirror(self, identifier)
        print mb.conn.server_show_template % mb.conn.server2dict(mirror)


    def do_test(self, subcmd, opts, identifier):
        """${cmd_name}: test if a mirror is working

        ${cmd_usage}
        ${cmd_option_list}
        """

        mirror = lookup_mirror(self, identifier)
        print mirror.baseurl
        import mb.testmirror
        mb.testmirror.access_http(mirror.baseurl)


    def do_probefile(self, subcmd, opts, filename):
        """${cmd_name}: list mirrors on which a given file is present
        by probing them

        ${cmd_usage}
        ${cmd_option_list}
        """

        from sqlobject.sqlbuilder import AND
        import mb.testmirror

        print filename

        mirrors = self.conn.Server.select(
                     AND(self.conn.Server.q.statusBaseurl == 1, 
                         self.conn.Server.q.enabled ==1))

        found_mirrors = 0
        for mirror in mirrors:
            # TODO: add a nice library function for this
            response = mb.testmirror.head_req(mirror.baseurl + filename)
            print response, mirror.identifier, mirror.baseurl
            # FIXME: the response code isn't usable on FTP urls.
            if response == 200: found_mirrors += 1

        print found_mirrors



    def do_edit(self, subcmd, opts, identifier):
        """${cmd_name}: edit a new mirror entry in $EDITOR

        Usage:
            mirrordoctor edit IDENTIFIER
        ${cmd_option_list}
        """
        mirror = lookup_mirror(self, identifier)
        
        import mb.conn
        old_dict = mb.conn.server2dict(mirror)
        old = mb.conn.server_show_template % old_dict

        import mb.util
        new = mb.util.edit_file(old)
        if not new:
            print 'Quitting.'
        else:
            new_dict = mb.conn.servertext2dict(new)

            for i in mb.conn.server_editable_attrs:
                if str(old_dict[i]) != new_dict[i]:
                    print """changing %s from '%s' to '%s'""" \
                            % (i, old_dict[i], new_dict[i])
                    a = new_dict[i]
                    if type(getattr(mirror, i)) == type(1L):
                        a = int(a)
                    setattr(mirror, i, a)



    def do_delete(self, subcmd, opts, identifier):
        """${cmd_name}: delete a mirror from the database

        ${cmd_usage}
        ${cmd_option_list}
        """
        
        if not identifier:
            sys.exit('need to specify identifier')

        s = self.conn.Server.select(self.conn.Server.q.identifier == identifier)
        for i in s:
            print self.conn.Server.delete(i.id)


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
            mirrordoctor scan [OPTS] IDENTIFIER [IDENTIFIER...]
        ${cmd_option_list}
        """

        import os
        cmd = opts.scanner or '/usr/bin/scanner'
        cmd += ' '
        if self.options.brain_instance:
            cmd += '-b %s ' % self.options.brain_instance
        if opts.enable:
            cmd += '-e '
        if opts.directory:
            cmd += '-k -x -d %s ' % opts.directory
        if opts.jobs:
            cmd += '-j %s ' % opts.jobs
        if opts.all:
            cmd += '-a '
        else:
            cmd += '-f '

        mirrors = []
        for arg in args:
            mirrors.append(lookup_mirror(self, arg))

        cmd += ' '.join([mirror.identifier for mirror in mirrors])

        if self.options.debug:
            print cmd

        rc = os.system(cmd)

        if opts.enable and rc == 0:
            import time
            comment = ('*** scanned and enabled at %s.' % (time.ctime()))
            for mirror in mirrors:
                mirror.comment = ' '.join([mirror.comment or '', '\n\n' + comment])



    def do_score(self, subcmd, opts, *args):
        """${cmd_name}: show or change the score of a mirror

        IDENTIFIER can be either the identifier or a substring.

        Usage:
            mirrordoctor score IDENTIFIER [SCORE]
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
          mirrordoctor file ls /path/to/xorg-x11-libXfixes-7.4-1.14.i586.rpm
          mirrordoctor file ls '*xorg-x11-libXfixes-7.4-1.14.i586.rpm'
          mirrordoctor file add distribution/11.0/SHOULD_NOT_BE_VISIBLE -m cdn.novell.com
          mirrordoctor file rm distribution/11.0/SHOULD_NOT_BE_VISIBLE -m MIRROR


        ${cmd_usage}
        ${cmd_option_list}
        """
        
        if path.startswith('/'):
            path = path[1:]

        import mb.files

        if action in ['add', 'rm']:
            if not opts.mirror:
                sys.exit('this command needs to be used with -m')

        if opts.mirror:
            mirror = lookup_mirror(self, opts.mirror)
        else:
            mirror = None

        if action == 'ls':
            rows = mb.files.ls(self.conn, path, mirror = mirror)

            for row in rows:
                print '%s %s %4d %s %s %-30s %s%s' % \
                        (row['region'].lower(), row['country'].lower(),
                         row['score'], 
                         row['enabled'] == 1 and 'ok      ' or 'disabled',
                         row['status_baseurl'] == 1 and 'ok  ' or 'dead',
                         row['identifier'], 
                         row['baseurl'], row['path'])



        elif action == 'add':
            mb.files.add(self.conn, path, mirror)

        elif action == 'rm':
            mb.files.rm(self.conn, path, mirror)

        else:
            sys.exit('ACTION must be either ls, rm or add.')


    @cmdln.option('-I', '--inline-images-from', metavar='PATH',
                  help='path to a directory with flag images to be inlined')
    @cmdln.option('-i', '--image-type', default='png', metavar='TYPE',
                  help='image file extension, e.g. png or gif')
    @cmdln.option('-f', '--format', default='txt', metavar='FORMAT',
                  help='output format of the mirrorlist, one of txt,txt2,xhtml')
    @cmdln.option('-l', '--list-markers', action='store_true',
                  help='just show the defined marker files.')
    def do_mirrorlist(self, subcmd, opts, *args):
        """${cmd_name}: generate a mirror list

        Generate a tabular mirror list, showing which subtree is
        listed on which mirrors.
        
        The presence of a particular subtree on the mirrors is determined
        by looking for "marker files", which are defined in the database for
        this purpose.


        Usage:
            mirrordoctor mirrorlist [IDENTIFIER]
        ${cmd_option_list}
        """
        
        import mb.files

        markers = self.conn.Marker.select()

        if opts.list_markers:
            for i in markers:
                print '%-20s    %r' % (i.subtreeName, i.markers.split())
            sys.exit(0)
        

        if args:
            mirrors = mb.conn.servers_match(self.conn.Server, args[0])
        else:
            mirrors = self.conn.Server.select(self.conn.Server.q.enabled == 1,
                                              orderBy=['region', 'country', '-score'])

        if opts.format == 'txt':
            for mirror in mirrors:
                print
                print mirror.identifier
                #print mirror.identifier, mirror.baseurl, mirror.baseurlFtp, mirror.baseurlRsync, mirror.score

                for marker in markers:
                    if mb.files.check_for_marker_files(self.conn, marker.markers, mirror.id):
                        print '+' + marker.subtreeName
                    else:
                        print '-' + marker.subtreeName


        elif opts.format == 'txt2':
            for mirror in mirrors:
                for marker in markers:
                    if mb.files.check_for_marker_files(self.conn, marker.markers, mirror.id):
                        print '%s: %s' % (mirror.identifier, marker.subtreeName)


        elif opts.format == 'xhtml':

            if opts.inline_images_from:
                import os
                import mb.util
                if not os.path.exists(opts.inline_images_from):
                    sys.exit('path %r does not exist')

            table_start, table_end = '<table>', '</table>'
            row_start, row_end = '  <tr>', '  </tr>'

            table_header_template = """\
    <th>Country</th>
    <th>Mirror</th>
    <th colspan="3">URL</th>
    <th>Priority</th>
"""
            row_template = """\
    <td><img src="%(img_link)s" width="16" height="11" alt="%(country_code)s"" />
        %(country_name)s
    </td>
    <td>%(identifier)s</td>
    <td>%(http_link)s</td>
    <td>%(ftp_link)s</td>
    <td>%(rsync_link)s</td>
    <td>%(prio)s</td>
"""

            href = lambda x, y: x and '<a href="%s">%s</a>' % (x, y)  or '' # 'n/a'

            def imgref(country_code):
                if not opts.inline_images_from:
                    return 'flags/%s.%s' % (country_code, opts.image_type)
                else:
                    return mb.util.data_url(opts.inline_images_from, 
                                            country_code + '.' + opts.image_type)


            last_region = 'we have not started yet...'

            for mirror in mirrors:
                region = mirror.region.lower()
                if region != last_region:
                    # new region block
                    if last_region != 'we have not started yet...':
                        print table_end

                    print '\n\n<h1>Region: %s</h1>\n' % region
                    print table_start
                    print row_start
                    print table_header_template
                    for marker in markers:
                        print '    <th>%s</th>' % marker.subtreeName
                    print row_end
                last_region = region

                country_name = self.conn.Country.select(
                        self.conn.Country.q.code == mirror.country.lower())[0].name
                map = { 'country_code': mirror.country.lower(),
                        'country_name': country_name,
                        'img_link':   imgref(mirror.country.lower()),
                        'region':     region,
                        'identifier': mirror.identifier,
                        'http_link':  href(mirror.baseurl, 'HTTP'),
                        'ftp_link':   href(mirror.baseurlFtp, 'FTP'),
                        'rsync_link': href(mirror.baseurlRsync, 'rsync'),
                        'prio':       mirror.score,
                        }
                
                print row_start
                print row_template % map
                
                for marker in markers:
                    if mb.files.check_for_marker_files(self.conn, marker.markers, mirror.id):
                        print '    <td>X</td>'
                    else:
                        print '    <td> </td>'

                print row_end

            print table_end


    @cmdln.option('--project', metavar='PROJECT',
                  help='Specify a project name.')
    def do_export(self, subcmd, opts, *args):
        """${cmd_name}: export the mirror list as text file

        Output format is suitable to be used in a Django ORM.

        ${cmd_usage}
        ${cmd_option_list}
        """

        if not opts.project:
            sys.exit('specify a project name with --project')

        print """#!/usr/bin/env python
import os, sys

mybasepath = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, mybasepath)
os.environ['DJANGO_SETTINGS_MODULE'] = 'mirrordjango.settings'

from django.db import connection

from mirrordjango.mb.models import Contact, Operator, Project, Server, Mirror

"""

        print """Project(name='%s').save()""" % opts.project

        django_template = """\

# ------------------------------------------------------------
try:
    c = Contact.objects.get_or_create(username=%(admin)r, password='UNSET', name=%(admin)r, email=%(adminEmail)r)[0]
except:
    connection.connection.rollback()
    c = None
o = Operator.objects.get_or_create(name='%(identifier)s', logo='')[0]
p = Project.objects.filter(name='""" + opts.project + """')
p = p[0]
s = Server.objects.get_or_create(identifier='%(identifier)s', operator=o, region='%(region)s', country='%(country)s', country_only='%(countryOnly)s', region_only='%(regionOnly)s', other_countries=%(otherCountries)r, file_maxsize='%(fileMaxsize)s', comment=%(comment)r, bandwidth=1)[0]
m = Mirror.objects.get_or_create(http='%(baseurl)s', ftp='%(baseurlFtp)s', rsync='%(baseurlRsync)s', prio='%(score)s', project=p, server=s)[0]
# s.mirrors.add(m)
if c:
    s.contacts.add(c)

"""

        mirrors = self.conn.Server.select()
        for i in mirrors:
            if i.comment == None:
                #print 'null comment', i
                i.comment = ''
            d = mb.conn.server2dict(i)
            print django_template % d


if __name__ == '__main__':
    import sys
    mirrordoctor = MirrorDoctor()
    sys.exit( mirrordoctor.main() )

