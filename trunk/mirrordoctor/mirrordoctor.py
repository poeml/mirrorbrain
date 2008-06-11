#!/usr/bin/python

"""
Script to maintain the mirror database

Requirements:
cmdln from http://trentm.com/projects/cmdln/
"""

__version__ = '1.0'
__author__ = 'Peter Poeml <poeml@suse.de>'
__copyright__ = 'Novell / SUSE Linux Products GmbH'
__license__ = 'GPL'
__url__ = 'http://mirrorbrain.org'



import cmdln
from mb.conn import *
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


class Container:
    def __init__(self):
        pass



class MirrorDoctor(cmdln.Cmdln):

    def get_optparser(self):
        """Parser for global options (that are not specific to a subcommand)"""
        #optparser = cmdln.CmdlnOptionParser(self, version=get_version())
        optparser = cmdln.CmdlnOptionParser(self)
        optparser.add_option('-d', '--debug', action='store_true',
                             help='print info useful for debugging')
        return optparser

    def postoptparse(self):
        """runs after parsing global options"""
        if self.options.debug:
            Server._connection.debug = True


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

    @cmdln.option('-i', '--identifier', metavar='ARG',
                        help='identifier string')

    def do_new(self, subcmd, opts):
        """${cmd_name}: insert a new mirror into the database


        example:
            mirrorbrain.py new -i lockdownhosting.com \
                -H http://mirror1.lockdownhosting.com/pub/opensuse/ \
                -F ftp://mirror1.lockdownhosting.com/pub/opensuse/ \
                -R rsync://mirror1.lockdownhosting.com/opensuse/ \
                -a 'He Who Never Sleeps' \
                -e sleep@example.com

        ${cmd_usage}
        ${cmd_option_list}
        """

        import urlparse

        #new = Container()

        if not opts.identifier:
            sys.exit('identifier needs to be specified')
        if not opts.http:
            sys.exit('HTTP baseurl needs to be specified')
        #if not opts.http and not opts.ftp:
        #    sys.exit('rsync or FTP baseurl needs to be specified')

        scheme, host, path, a, b, c = urlparse.urlparse(opts.http)
        if not opts.region:
            opts.region = mb.geoip.lookup_region_code(host)
        if not opts.country:
            opts.country = mb.geoip.lookup_country_code(host)

        if opts.region == '--' or opts.country == '--':
            raise ValueError('region lookup failed')

        s = Server(identifier   = opts.identifier,
                   baseurl      = opts.http,
                   baseurlFtp   = opts.ftp or '',
                   baseurlRsync = opts.rsync,
                   region       = opts.region,
                   country      = opts.country,
                   score        = opts.score,
                   admin        = opts.admin,
                   adminEmail   = opts.admin_email,
                   comment      = opts.comment)
        print s


    @cmdln.option('-m', '--match', metavar='EXPR',
                        help='show only mirrors whose identifier matches EXPR (SQL syntax)')
    @cmdln.option('-c', '--country', metavar='XY',
                        help='show only mirrors whose country matches XY')
    def do_list(self, subcmd, opts):
        """${cmd_name}: list mirrors

        ${cmd_usage}
        ${cmd_option_list}
        """
        from sqlobject.sqlbuilder import LIKE
        if opts.match:
            mirrors = Server.select("""identifier LIKE '%%%s%%'""" % opts.match)
        elif opts.country:
            mirrors = Server.select("""country LIKE '%%%s%%'""" % opts.country)
        else:
            mirrors = Server.select()

        for mirror in mirrors:
            print mirror.identifier


    @cmdln.option('-m', '--match', metavar='EXPR',
                        help='show only mirrors whose identifier matches EXPR (SQL syntax)')
    def do_show(self, subcmd, opts, *args):
        """${cmd_name}: show a new mirror entry

        ${cmd_usage}
        ${cmd_option_list}
        """

        if opts.match:
            mirrors = Server.select("""identifier LIKE '%%%s%%'""" % opts.match)
            if len(list(mirrors)) == 0:
                sys.exit('Not found.')
            elif len(list(mirrors)) == 1:
                s = mirrors.getOne()
            else:
                print 'Found multiple matching mirrors:'
                for i in list(mirrors):
                    print i.identifier
                sys.exit(1)
        else:
            identifier = args[0]
            mirrors = Server.select(Server.q.identifier == identifier)
            s = mirrors.getOne()

        print server_show_template % server2dict(s)


    @cmdln.option('-m', '--match', metavar='EXPR',
                        help='edit the mirror whose identifier matches EXPR (SQL syntax)')
    def do_edit(self, subcmd, opts, *args):
        """${cmd_name}: edit a new mirror entry in $EDITOR

        ${cmd_usage}
        ${cmd_option_list}
        """
        if opts.match:
            mirrors = Server.select("""identifier LIKE '%%%s%%'""" % opts.match)
            if len(list(mirrors)) == 0:
                sys.exit('Not found.')
            elif len(list(mirrors)) == 1:
                s = mirrors.getOne()
            else:
                print 'Found multiple matching mirrors:'
                for i in list(mirrors):
                    print i.identifier
                sys.exit(1)
        else:
            identifier = args[0]
            mirrors = Server.select(Server.q.identifier == identifier)
            s = mirrors.getOne()

        
        old_dict = server2dict(s)
        old = server_show_template % old_dict

        import mb.util
        new = mb.util.edit_file(old)
        if not new:
            print 'Quitting.'
        else:
            new_dict = servertext2dict(new)

            for i in server_editable_attrs:
                if str(old_dict[i]) != new_dict[i]:
                    print """changing %s from '%s' to '%s'""" \
                            % (i, old_dict[i], new_dict[i])
                    a = new_dict[i]
                    if type(getattr(s, i)) == type(1L):
                        a = int(a)
                    setattr(s, i, a)



    def do_delete(self, subcmd, opts, identifier):
        """${cmd_name}: delete a mirror from the database

        ${cmd_usage}
        ${cmd_option_list}
        """
        
        if not identifier:
            sys.exit('need to specify identifier')

        s = Server.select(Server.q.identifier == identifier)
        for i in s:
            print Server.delete(i.id)


    @cmdln.option('-C', '--comment', metavar='ARG',
                        help='comment string to append')
    def do_commentadd(self, subcmd, opts, identifier):
        """${cmd_name}: add a comment about a mirror 

        ${cmd_usage}
        ${cmd_option_list}
        """
        
        if not opts.comment:
            sys.exit('need to specify comment to add')

        s = Server.select(Server.q.identifier == identifier)
        for i in s:
            i.comment = ' '.join([i.comment or '', opts.comment])


    def do_enable(self, subcmd, opts, identifier):
        """${cmd_name}: enable a mirror 

        ${cmd_usage}
        ${cmd_option_list}
        """
        
        s = Server.select(Server.q.identifier == identifier)
        mirror = s.getOne()
        mirror.enabled = 1


    def do_disable(self, subcmd, opts, identifier):
        """${cmd_name}: disable a mirror

        ${cmd_usage}
        ${cmd_option_list}
        """
        
        s = Server.select(Server.q.identifier == identifier)
        mirror = s.getOne()
        mirror.enabled = 0


    @cmdln.option('-f', '--force', action='store_true',
                  help='Force. Scan listed mirror_ids even if they are not enabled.')
    @cmdln.option('-e', '--enable', action='store_true',
                  help='Enable mirror, after it was scanned. Useful with -f')
    def do_scan(self, subcmd, opts, identifier):
        """${cmd_name}: scan a mirror

        ${cmd_usage}
        ${cmd_option_list}
        """

        import os
        cmd = '/usr/bin/scanner '
        if opts.force:
            cmd += '-f '
        if opts.enable:
            cmd += '-e '
        cmd += identifier

        os.system(cmd)



if __name__ == '__main__':
    import sys
    mirrordoctor = MirrorDoctor()
    sys.exit( mirrordoctor.main() )

