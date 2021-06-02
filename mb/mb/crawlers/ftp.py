import os
import sys
import socket
import urllib
from ftplib import FTP

from mb.util import Afile
from mb.mberr import *


DEBUG = 0
socket.setdefaulttimeout(120)


def line2File(line):
    """parse line from FTP LIST reply and return the filename

    Most FTP servers send lines with 9 parts, where the last is the filename
    (and the name could have spaces...)

    Some servers return one part less"""
    parts = line.split()
    if len(parts) == 9:
        name = parts[8]
        size = parts[4]
    elif len(parts) == 8:
        name = parts[7]
        size = parts[3]
    else:
        raise Exception('haeh? could not parse line from FTP listing')
    return Afile(name, size)


def gen_ftp(url):
    """connect to FTP server and return an iterator of found files
    below the given url"""

    (scheme, host, start_dir, params, query, fragment) = urllib.urlparse(url)
    if ':' in host:
        host, port = host.split(':')
    else:
        port = 21

    ftp = FTP()

    try:
        ftp.connect(host, port)

        ftp.login()
        if DEBUG:
            ftp.set_debuglevel(2)
        welcome = ftp.getwelcome()
        if DEBUG:
            print '--- welcome:'
            print welcome

        # queue of directories to look into
        queue = [start_dir]
        directory = start_dir
        # print '--> changing to', directory
        # ftp.cwd(directory)

        # walk directories non-recursively
        # http://aspn.activestate.com/ASPN/Cookbook/Python/Recipe/435875
        while len(queue) > 0:
            if DEBUG:
                print 'still %s directories to do' % len(queue)

            directory = queue.pop(0)

            listing = []
            ftp.dir(directory, listing.append)

            if DEBUG:
                print 'entering directory', directory

            if DEBUG:
                print '--- listing:'
                print '\n'.join(listing)
                print '---'

            if DEBUG:
                print 'files: ', len(listing)
            for line in listing:

                if line.startswith('-'):
                    f = line2File(line)
                    f.path = os.path.join(directory[len(start_dir):], f.name)
                    yield f

                elif line.startswith('d'):
                    dirname = line2File(line).name
                    if dirname in ['.', '..']:
                        continue
                    dirname2 = os.path.join(start_dir, directory, dirname)
                    if DEBUG:
                        print 'directory name:', dirname2
                    queue.append(dirname2)

                elif line.startswith('l'):
                    # we ignore links
                    pass

        ftp.quit()

    except socket.error, e:
        raise SocketError(url, e.__str__())
