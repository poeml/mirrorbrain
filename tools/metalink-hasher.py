#!/usr/bin/python

"""
Script to create metalink hashes

Requirements:
cmdln from http://trentm.com/projects/cmdln/
metalink commandline tool from http://metamirrors.nl/metalinks_project
"""

__version__ = '1.0'
__author__ = 'Peter Poeml <poeml@suse.de>'
__copyright__ = 'Peter poeml <poeml@suse.de>'
__license__ = 'GPL'
__url__ = 'http://mirrorbrain.org'


import os, os.path
import cmdln
import re

ML_EXTENSION = '.metalink-hashes'

def make_hashes(src, dst, opts):
    src_dir = os.path.dirname(src)
    src_dir_mode = os.stat(src_dir).st_mode
    dst_dir = os.path.dirname(dst)

    dst = dst + ML_EXTENSION

    if not opts.dry_run:
        if not os.path.isdir(dst_dir):
            os.makedirs(dst_dir, mode = 0755)
        os.chmod(dst_dir, src_dir_mode)

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

    cmd = 'metalink --nomirrors -d md5 -d sha1 -d sha1pieces "%s"' % src
    cmd += ' | grep "</\?\(verification\|hash\|pieces\).*>" > "%s"' % dst

    if opts.verbose:
        print cmd

    if not opts.dry_run: 
        try:
            os.system(cmd)

            src_mode = os.stat(src).st_mode
            os.chmod(dst, src_mode)
        except:
            os.unlink(dst)
            raise


class Metalinks(cmdln.Cmdln):

    @cmdln.option('-n', '--dry-run', action='store_true',
                        help='don\'t actually do anything')
    @cmdln.option('-f', '--file-mask', metavar='REGEX',
                        help='regular expression to select files to create hashes for')
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


        while len(directories)>0:
            directory = directories.pop()

            for name in os.listdir(directory):

                fullpath = os.path.join(directory,name)

                if os.path.islink(fullpath):
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
