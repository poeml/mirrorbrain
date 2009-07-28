#!/usr/bin/python

# metalink-hasher -- create metalink hashes
# 
# This script requires the cmdln module, which you can obtain here:
# http://trentm.com/projects/cmdln/
# and the metalink commandline tool, which you can find here:
# http://metamirrors.nl/metalinks_project
# 
# 
# Copyright 2008,2009 Peter Poeml
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


__version__ = '1.1'
__author__ = 'Peter Poeml <poeml@suse.de>'
__copyright__ = 'Peter poeml <poeml@suse.de>'
__license__ = 'GPLv2'
__url__ = 'http://mirrorbrain.org'


import os, os.path
import cmdln
import re
import subprocess

ML_EXTENSION = '.metalink-hashes'
line_mask = re.compile('.*</*(verification|hash|pieces).*>.*')

def make_hashes(src, dst, opts):
    src_dir = os.path.dirname(src)
    src_dir_mode = os.stat(src_dir).st_mode
    dst_dir = os.path.dirname(dst)

    dst = dst + ML_EXTENSION

    if not opts.dry_run:
        if not os.path.isdir(dst_dir):
            os.makedirs(dst_dir, mode = 0755)
        if opts.copy_permissions:
            os.chmod(dst_dir, src_dir_mode)
        else:
            os.chmod(dst_dir, 0755)

    src_mtime = os.path.getmtime(src)
    try:
        dst_mtime = os.path.getmtime(dst)
        dst_size = os.path.getsize(dst)
    except OSError:
        dst_mtime = dst_size = 0 # file missing

    if dst_mtime >= src_mtime and dst_size != 0:
        if opts.verbose:
            print 'up to date:', src
        return 

    cmd = [ 'metalink',
            '--nomirrors', 
            '-d', 'md5', 
            '-d', 'sha1', 
            '-d', 'sha1pieces',
            src ]

    if opts.verbose or opts.dry_run:
        print ' '.join(cmd)

    if opts.dry_run: 
        return

    o = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                    close_fds=True).stdout
    lines = []
    for line in o.readlines():
        if re.match(line_mask, line):
            lines.append(line)


    # if present, add PGP signature into the <verification> block
    if os.path.exists(src + '.asc'):
        sig = open(src + '.asc').read()
        sig = '\t\t\t<signature type="pgp" file="%s.asc">\n\n' % os.path.basename(src) + \
              sig + \
              '\n\t\t\t</signature>\n'

        lines.insert(1, sig)

    d = open(dst, 'wb')
    d.write(''.join(lines))
    d.close()

    if opts.copy_permissions:
        os.chmod(dst, os.stat(src).st_mode)
    else:
        os.chmod(dst, 0644)


class Metalinks(cmdln.Cmdln):

    @cmdln.option('-n', '--dry-run', action='store_true',
                        help='don\'t actually do anything')
    @cmdln.option('--copy-permissions', action='store_true',
                        help='copy the permissions of directories and files '
                             'to the hashes files. Normally, this should not '
                             'be needed, because the hash files don\'t contain '
                             'any reversible information.')
    @cmdln.option('-f', '--file-mask', metavar='REGEX',
                        help='regular expression to select files to create hashes for')
    @cmdln.option('-i', '--ignore-mask', metavar='REGEX',
                        help='regular expression to ignore certain files, and don\'t create hashes for them')
    @cmdln.option('-b', '--base-dir', metavar='PATH',
                        help='set the base directory (so that you can work on a subdirectory)')
    @cmdln.option('-t', '--target-dir', metavar='PATH',
                        help='set a different target directory')
    @cmdln.option('-v', '--verbose', action='store_true',
                        help='show more information')
    def do_update(self, subcmd, opts, startdir):
        """${cmd_name}: Update the hash pieces that are included in metalinks

        Example:

        metalink-hasher update \\
           -f '.*.(torrent|iso)$' \\
          -t /var/lib/apache2/metalink-hashes/srv/ftp/pub/opensuse/distribution/11.0/iso \\
          -b /srv/ftp-stage/pub/opensuse/distribution/11.0/iso \\
          /srv/ftp-stage/pub/opensuse/distribution/11.0/iso \\
          -n

        ${cmd_usage}
        ${cmd_option_list}
        """

        if not opts.target_dir:
            sys.exit('You must specify the target directory (-t)')
        if not opts.base_dir:
            sys.exit('You must specify the base directory (-b)')

        if not opts.target_dir.startswith('/'):
            sys.exit('The target directory must be an absolut path')
        if not opts.base_dir.startswith('/'):
            sys.exit('The base directory must be an absolut path')

        startdir = startdir.rstrip('/')
        opts.target_dir = opts.target_dir.rstrip('/')
        opts.base_dir = opts.base_dir.rstrip('/')

        directories = [startdir]

        if opts.ignore_mask: 
            opts.ignore_mask = re.compile(opts.ignore_mask)
        if opts.file_mask: 
            opts.file_mask = re.compile(opts.file_mask)

        while len(directories)>0:
            directory = directories.pop()

            for name in os.listdir(directory):

                fullpath = os.path.join(directory,name)

                if os.path.islink(fullpath):
                    continue

                if opts.ignore_mask and re.match(opts.ignore_mask, fullpath):
                    continue

                if os.path.isfile(fullpath):
                    if not opts.file_mask or re.match(opts.file_mask, name):
                        #print fullpath
                        if opts.base_dir:
                            target = fullpath[len(opts.base_dir):]
                        else:
                            target = fullpath
                        target = os.path.join(opts.target_dir, target.lstrip('/'))
                        if opts.verbose:
                            print 'target:', target
                        make_hashes(fullpath, target, opts=opts)

                elif os.path.isdir(fullpath):
                    directories.append(fullpath)  # It's a directory, store it.


if __name__ == '__main__':
    import sys
    metalinks = Metalinks()
    sys.exit( metalinks.main() )