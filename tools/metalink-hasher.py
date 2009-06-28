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


__version__ = '1.2'
__author__ = 'Peter Poeml <poeml@suse.de>'
__copyright__ = 'Peter poeml <poeml@suse.de>'
__license__ = 'GPLv2'
__url__ = 'http://mirrorbrain.org'


import os
import os.path
import stat
import shutil
import cmdln
import re
import subprocess
import errno

ML_EXTENSION = '.metalink-hashes'
line_mask = re.compile('.*</*(verification|hash|pieces).*>.*')

def make_hashes(src, src_statinfo, dst, opts):

    try:
        dst_statinfo = os.stat(dst)
        dst_mtime = dst_statinfo.st_mtime
        dst_size = dst_statinfo.st_size
    except OSError:
        dst_mtime = dst_size = 0 # file missing

    if dst_mtime >= src_statinfo.st_mtime and dst_size != 0:
        if opts.verbose:
            print 'Up to date: %r' % dst
        return 

    cmd = [ 'metalink',
            '--nomirrors', 
            '-d', 'md5', 
            '-d', 'sha1', 
            '-d', 'sha256', 
            '-d', 'sha1pieces',
            src ]

    if opts.dry_run: 
        print 'Would run: ', ' '.join(cmd)
        return

    sys.stdout.flush()
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
        os.chmod(dst, src_statinfo.st_mode)
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

        Examples:

        metalink-hasher update /srv/mirrors/mozilla -t /srv/metalink-hashes/srv/mirrors/mozilla

        metalink-hasher update \\
            -t /srv/metalink-hashes/srv/ftp/pub/opensuse/repositories/home:/poeml \\
            /srv/ftp-stage/pub/opensuse/repositories/home:/poeml \\
            -i '^.*/repoview/.*$'

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
            opts.base_dir = startdir
            #sys.exit('You must specify the base directory (-b)')

        if not opts.target_dir.startswith('/'):
            sys.exit('The target directory must be an absolut path')
        if not opts.base_dir.startswith('/'):
            sys.exit('The base directory must be an absolut path')

        startdir = startdir.rstrip('/')
        opts.target_dir = opts.target_dir.rstrip('/')
        opts.base_dir = opts.base_dir.rstrip('/')

        directories_todo = [startdir]

        if opts.ignore_mask: 
            opts.ignore_mask = re.compile(opts.ignore_mask)
        if opts.file_mask: 
            opts.file_mask = re.compile(opts.file_mask)

        unlinked_files = unlinked_dirs = 0

        while len(directories_todo) > 0:
            src_dir = directories_todo.pop()

            src_dir_mode = os.stat(src_dir).st_mode

            dst_dir = os.path.join(opts.target_dir, src_dir[len(opts.base_dir):].lstrip('/'))

            if not opts.dry_run:
                if not os.path.isdir(dst_dir):
                    os.makedirs(dst_dir, mode = 0755)
                if opts.copy_permissions:
                    os.chmod(dst_dir, src_dir_mode)
                else:
                    os.chmod(dst_dir, 0755)

            src_names = set(os.listdir(src_dir))
            try:
                dst_names = os.listdir(dst_dir)
                dst_names.sort()
            except OSError, e:
                if e.errno == errno.ENOENT:
                    sys.exit('\nSorry, cannot really continue in dry-run mode, because directory %r does not exist.\n'
                             'You might want to create it:\n'
                             '  mkdir %s' % (dst_dir, dst_dir))

            for i in dst_names:
                i_path = os.path.join(dst_dir, i)
                # removal of obsolete files
                if i.endswith(ML_EXTENSION):
                    realname = i[:-len(ML_EXTENSION)]
                    if (realname not in src_names) \
                       or (opts.ignore_mask and re.match(opts.ignore_mask, i_path)):
                        print 'Unlinking obsolete %r' % i_path
                        if not opts.dry_run: 
                            try:
                                os.unlink(i_path)
                            except OSError, e:
                                print 'Unlink failed for %r: %s' % (i_path, e.errno)
                        unlinked_files += 1
                # removal of obsolete directories
                else:
                    if i not in src_names:
                        if os.path.isdir(i_path):
                            print 'Recursively removing obsolete directory %r' % i_path
                            if not opts.dry_run: 
                                try:
                                    shutil.rmtree(i_path)
                                except OSError, e:
                                    print 'Recursive unlinking failed for %r: %s' % (i_path, e.errno)
                            unlinked_dirs += 1
                        else:
                            print 'Unlinking obsolete %r' % i_path
                            if not opts.dry_run: 
                                os.unlink(i_path)
                            unlinked_files += 1

            for src_name in sorted(src_names):

                src = os.path.join(src_dir, src_name)

                if opts.ignore_mask and re.match(opts.ignore_mask, src):
                    continue

                # stat only once
                src_statinfo = os.lstat(src)
                if stat.S_ISLNK(src_statinfo.st_mode):
                    #print 'ignoring link', src
                    continue

                if stat.S_ISREG(src_statinfo.st_mode):
                    if not opts.file_mask or re.match(opts.file_mask, src_name):

                        dst_name = src[len(opts.base_dir):].lstrip('/')
                        dst = os.path.join(opts.target_dir, dst_name)
                        #if opts.verbose:
                        #    print 'dst:', dst

                        make_hashes(src, src_statinfo, dst + ML_EXTENSION, opts=opts)


                elif stat.S_ISDIR(src_statinfo.st_mode):
                    directories_todo.append(src)  # It's a directory, store it.


        if  unlinked_files or unlinked_dirs:
            print 'Unlinked %s files, %d directories.' % (unlinked_files, unlinked_dirs)



if __name__ == '__main__':
    import sys
    metalinks = Metalinks()
    sys.exit( metalinks.main() )
