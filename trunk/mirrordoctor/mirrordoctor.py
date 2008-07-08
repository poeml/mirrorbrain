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

def lookup_mirror(identifier):

    r = servers_match(identifier)

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
            mirrors = Server.select("""country LIKE '%%%s%%'""" % opts.country)
        elif opts.region:
            mirrors = Server.select("""region LIKE '%%%s%%'""" % opts.region)
        elif args:
            mirrors = servers_match(args[0])
        else:
            mirrors = Server.select()

        for mirror in mirrors:
            print mirror.identifier


    def do_show(self, subcmd, opts, identifier):
        """${cmd_name}: show a new mirror entry

        ${cmd_usage}
        ${cmd_option_list}
        """

        mirror = lookup_mirror(identifier)
        print server_show_template % server2dict(mirror)


    def do_edit(self, subcmd, opts, identifier):
        """${cmd_name}: edit a new mirror entry in $EDITOR

        Usage:
            mirrordoctor edit IDENTIFIER
        ${cmd_option_list}
        """
        mirror = lookup_mirror(identifier)
        
        old_dict = server2dict(mirror)
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

        mirror = lookup_mirror(identifier)
        mirror.comment = ' '.join([mirror.comment or '', '\n\n' + opts.comment])


    def do_enable(self, subcmd, opts, identifier):
        """${cmd_name}: enable a mirror 

        ${cmd_usage}
        ${cmd_option_list}
        """
        
        mirror = lookup_mirror(identifier)
        mirror.enabled = 1


    def do_disable(self, subcmd, opts, identifier):
        """${cmd_name}: disable a mirror

        ${cmd_usage}
        ${cmd_option_list}
        """
        
        mirror = lookup_mirror(identifier)
        mirror.enabled = 0


    def do_rename(self, subcmd, opts, identifier, new_identifier):
        """${cmd_name}: rename a mirror's identifier

        ${cmd_usage}
        ${cmd_option_list}
        """
        
        mirror = lookup_mirror(identifier)
        mirror.identifier = new_identifier


    @cmdln.option('-f', '--force', action='store_true',
                  help='Force. Scan listed mirror ids even if they are not enabled.')
    @cmdln.option('-e', '--enable', action='store_true',
                  help='Enable a mirror, after it was scanned. Useful with -f')
    @cmdln.option('-j', '--jobs', metavar='N',
                  help='Run up to N scanner queries in parallel.')
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
        cmd = '/usr/bin/scanner '
        if opts.force:
            cmd += '-f '
        if opts.enable:
            cmd += '-e '
        if opts.directory:
            cmd += '-k -x -d %s ' % opts.directory
        if opts.jobs:
            cmd += '-j %s ' % opts.jobs

        mirrors = []
        for arg in args:
            mirrors.append(lookup_mirror(arg))

        cmd += ' '.join([mirror.identifier for mirror in mirrors])

        os.system(cmd)


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
        
        mirror = lookup_mirror(identifier)

        if not score:
            print mirror.score
        else:
            print 'Changing score for %s: %s -> %s' \
                    % (mirror.identifier, mirror.score, score)
            mirror.score = int(score)
        



if __name__ == '__main__':
    import sys
    mirrordoctor = MirrorDoctor()
    sys.exit( mirrordoctor.main() )

