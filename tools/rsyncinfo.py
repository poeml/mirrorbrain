#!/usr/bin/python

"""
Script to get info on modules on rsync servers

Requirements:
cmdln from http://trentm.com/projects/cmdln/
"""

__version__ = '1.0'
__author__ = 'Peter Poeml <poeml@suse.de>'
__copyright__ = 'Peter poeml <poeml@suse.de>'
__license__ = 'GPL'
__url__ = 'http://mirrorbrain.org'

import sys, commands
import cmdln


class RsyncInfo(cmdln.Cmdln):

    def get_optparser(self):
        """this is the parser for "global" options (not specific to subcommand)"""

        optparser = cmdln.CmdlnOptionParser(self)
        optparser.add_option('-d', '--debug', action='store_true',
                      help='print info useful for debugging')
        optparser.add_option('-n', '--dry-run', action='store_true',
                      help='just print out what would be done')
        return optparser


    def read_module_list(self, host):
        cmd = 'rsync -a %s::' % host
        if self.options.debug:
            print cmd
        if self.options.dry_run:
            return []
        out = commands.getoutput(cmd)
        out = out.splitlines()
        out.reverse()
        for idx, line in enumerate(out):
            if line == '' or line.startswith(' '):
                break
        out = out[:idx]
        out.reverse()

        mods = []
        for line in out:
            mods.append(line.split()[0])

        return mods


    def do_list(self, subcmd, opts, host):
        """${cmd_name}: list modules on an rsync server

        ${cmd_usage}
        ${cmd_option_list}
        """

        mods = self.read_module_list(host)
        for mod in mods:
            print mod


    @cmdln.option('-p', '--password', metavar='PASSWORD', default='',
                  help='optional password to send')
    @cmdln.option('-u', '--user', metavar='USER', default='',
                  help='optional username to send')
    @cmdln.option('-O', '--rsync-opts', metavar='OPT', action='append',
                  help='Additional options to pass to rsync call. '
                  'Can be given multiple times.')
    @cmdln.option('-m', '--modules', metavar='MOD', action='append',
                  help='Add this rsync module to the list. '
                  'Can be given multiple times.')
    @cmdln.option('-a', '--all-modules', action='store_true',
                  help='Use all existing rsync modules')
    def do_size(self, subcmd, opts, host):
        """${cmd_name}: find out the size of an rsync module

        Specify the name of the rsync modules with --modules, or with
        --all-modules.

        ${cmd_usage}
        ${cmd_option_list}
        """

        if opts.user:
            opts.user += '@'
        if opts.all_modules:
            opts.modules = self.read_module_list(host)

        for mod in opts.modules:
            cmd = 'RSYNC_PASSWORD=\'%s\' rsync -a %s%s::%s . --stats -n -h %s | awk \'/^Total file size/ { print $4; exit }\'' \
                        % (opts.password, opts.user, host, mod, ' '.join(opts.rsync_opts or ''))
            if self.options.debug:
                print cmd
            if not self.options.dry_run:
                size = commands.getoutput(cmd)
                print mod, size



if __name__ == '__main__':
    import sys
    rsyncinfo = RsyncInfo()
    try:
        rv = rsyncinfo.main()
    except KeyboardInterrupt:
        sys.exit('interrupted')

    sys.exit(rv)

