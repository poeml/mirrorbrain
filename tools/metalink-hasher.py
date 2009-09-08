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

line_mask = re.compile('.*</*(verification|hash|pieces).*>.*')

class Hasheable:
    """represent a file and its metadata"""
    def __init__(self, basename, src_dir=None, dst_dir=None):
        self.basename = basename
        if src_dir:
            self.src_dir = src_dir
        else:
            self.src_dir = os.path.dirname(self.basename)

        self.src = os.path.join(src_dir, self.basename)

        self.finfo = os.lstat(self.src)
        self.atime = self.finfo.st_atime
        self.mtime = self.finfo.st_mtime
        self.size  = self.finfo.st_size
        self.inode = self.finfo.st_ino
        self.mode  = self.finfo.st_mode

        self.dst_dir = dst_dir

        self.dst_basename = '%s.size_%s' % (self.basename, self.size)
        self.dst = os.path.join(self.dst_dir, self.dst_basename)

        # migration 2.10.0 -> 2.10.1
        self.dst_old_basename = '%s.inode_%s' % (self.basename, self.inode)
        self.dst_old = os.path.join(self.dst_dir, self.dst_old_basename)

    def islink(self):
        return stat.S_ISLNK(self.mode)
    def isreg(self):
        return stat.S_ISREG(self.mode)
    def isdir(self):
        return stat.S_ISDIR(self.mode)

    def do_hashes(self, verbose=False, dry_run=False, copy_permissions=True):
        try:
            dst_statinfo = os.stat(self.dst)
            dst_mtime = dst_statinfo.st_mtime
            dst_size = dst_statinfo.st_size
        except OSError:
            dst_mtime = dst_size = 0 # file missing

        if dst_mtime == self.mtime and dst_size != 0:
            if verbose:
                print 'Up to date: %r' % self.dst
            return 

        if os.path.exists(self.dst_old):
            # upgrade mode 2.10.0 -> 2.10.1
            print 'migrating %s -> %s' % (self.dst_old, self.dst)
            if not dry_run: 
                os.rename(self.dst_old, self.dst)
                os.utime(self.dst, (self.atime, self.mtime))
            return 

        cmd = [ 'metalink',
                '--nomirrors', 
                '-d', 'md5', 
                '-d', 'sha1', 
                '-d', 'sha256', 
                '-d', 'sha1pieces',
                self.src ]

        if dry_run: 
            print 'Would run: ', ' '.join(cmd)
            return

        sys.stdout.flush()
        o = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                        close_fds=True).stdout
        lines = []
        for line in o.readlines():
            if re.match(line_mask, line):
                line = line.replace('\t\t', ' ' * 6)
                lines.append(line)


        # if present, add PGP signature into the <verification> block
        if os.path.exists(self.src + '.asc'):
            sig = open(self.src + '.asc').read()
            sig = '        <signature type="pgp" file="%s.asc">\n' % self.basename + \
                  sig + \
                  '\n        </signature>\n'

            lines.insert(1, sig)

        d = open(self.dst, 'wb')
        d.write(''.join(lines))
        d.close()

        os.utime(self.dst, (self.atime, self.mtime))

        if copy_permissions:
            os.chmod(self.dst, self.mode)
        else:
            os.chmod(self.dst, 0644)

    #def __eq__(self, other):
    #    return self.basename == other.basename
    #def __eq__(self, basename):
    #    return self.basename == basename
        
    def __str__(self):
        return self.basename



class Metalinks(cmdln.Cmdln):

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
            except OSError, e:
                if e.errno == errno.ENOENT:
                    sys.exit('\nSorry, cannot really continue in dry-run mode, because directory %r does not exist.\n'
                             'You might want to create it:\n'
                             '  mkdir %s' % (dst_dir, dst_dir))


            # a set offers the fastest access for "foo in ..." lookups
            src_basenames = set(os.listdir(src_dir))
            #print 'doing', src_dir

            dst_keep = set()

            for src_basename in sorted(src_basenames):
                src = os.path.join(src_dir, src_basename)

                if opts.ignore_mask and re.match(opts.ignore_mask, src):
                    continue

                # stat only once
                try:
                    hasheable = Hasheable(src_basename, src_dir=src_dir, dst_dir=dst_dir)
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
                        hasheable.do_hashes(verbose=opts.verbose, 
                                            dry_run=opts.dry_run, 
                                            copy_permissions=opts.copy_permissions)
                        dst_keep.add(hasheable.dst_basename)

                elif hasheable.isdir():
                    directories_todo.append(src)  # It's a directory, store it.
                    dst_keep.add(hasheable.basename)


            dst_remove = set(dst_names) - dst_keep

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


        if  unlinked_files or unlinked_dirs:
            print 'Unlinked %s files, %d directories.' % (unlinked_files, unlinked_dirs)



if __name__ == '__main__':
    import sys
    metalinks = Metalinks()
    sys.exit( metalinks.main() )
