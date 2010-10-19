#!/usr/bin/python

# rsyncinfo - script to get info on modules on rsync servers
# 
# This script requires the cmdln module, which you can obtain here:
# http://trentm.com/projects/cmdln/
#
#
# Copyright 2007,2008,2009,2010 Peter Poeml
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License version 2
# as published by the Free Software Foundation;
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA



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
        has_header = False
        for idx, line in enumerate(out):
            if line == '' or line.startswith(' '):
                has_header = True
                break
        if has_header:
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


    @cmdln.option('-S', '--hide-stderr', action='store_true',
                  help='don\'t show stderr output of rsync')
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

        If the argument is a hostname, --modules or --all-modules is needed.
        If it is an rsync URL, you can omit those options.

        ${cmd_usage}
        ${cmd_option_list}
        """

        if opts.user:
            opts.user += '@'
        if opts.all_modules:
            opts.modules = self.read_module_list(host)

        mod_maxlen = 0
        if opts.modules:
            for mod in opts.modules:
                if len(mod) > mod_maxlen:
                    mod_maxlen = len(mod)
        else:
            if host.startswith('rsync://'):
                host = host[8:]


            # note: this parsing doesn't cater for embedded credentials
            if '::' in host:
                mod = host[host.find('::')+2 :]
                host = host[:host.find('::')]
            elif '/' in host:
                mod = host[host.find('/')+1 :]
                host = host[:host.find('/')]
            else:
                sys.exit('if -m is not used, the host string must contain a path (e.g. rsync URL)')

            opts.modules = [mod]
            mod_maxlen = len(mod)

        template = '%%-%ds %%10s' % mod_maxlen

        for mod in opts.modules:
            cmd = 'RSYNC_PASSWORD=\'%s\' rsync -a %s%s::%s . --stats -n -h %s' \
                        % (opts.password, opts.user, host, mod, ' '.join(opts.rsync_opts or ''))
            if opts.hide_stderr:
                cmd += ' 2> /dev/null'
            cmd += ' | awk \'/^Total file size/ { print $4; exit }\''
            if self.options.debug:
                print cmd
            if not self.options.dry_run:
                size = commands.getoutput(cmd)
                print template % (mod, size)



if __name__ == '__main__':
    import sys
    rsyncinfo = RsyncInfo()
    try:
        rv = rsyncinfo.main()
    except KeyboardInterrupt:
        sys.exit('interrupted')

    sys.exit(rv)

