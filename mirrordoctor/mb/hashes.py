#!/usr/bin/python

import os
import os.path
import stat

try:
    import hashlib
    md5 = hashlib
    sha1 = hashlib
    sha256 = hashlib
except ImportError:
    import md5
    md5 = md5
    import sha1
    sha1 = sha1
    # I guess that Python 2.4 didn't have a sha256 counterpart
    sha256 = None

PIECESIZE = 262144


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

        if int(dst_mtime) == int(self.mtime) and dst_size != 0:
            if verbose:
                print 'Up to date: %r' % self.dst
            return 

        if dry_run: 
            print 'Would make hashes for: ', self.src
            return

        digests = Digests(src = self.src)

        # if present, grab PGP signature
        if os.path.exists(self.src + '.asc'):
            digests.pgp = open(self.src + '.asc').read()

        digests.read()

        d = open(self.dst, 'wb')
        d.write(digests.dump_2_12_template())
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



class Digests():
    def __init__(self, src):
        self.src = src
        self.basename = os.path.basename(src)

        self.md5 = None
        self.sha1 = None
        self.sha256 = None
        self.pgp = None

        self.npieces = 0
        self.pieces = []


    def read(self):
        m = md5.md5()
        s1 = sha1.sha1()
        s256 = sha256.sha256()
        short_read_before = False

        f = open(self.src, 'rb')

        while 1 + 1 == 2:
            buf = f.read(PIECESIZE)
            if not buf: break

            if len(buf) != PIECESIZE:
                if not short_read_before:
                    short_read_before = True
                else:
                    raise('InternalError')

            m.update(buf)
            s1.update(buf)
            s256.update(buf)

            self.npieces += 1
            self.pieces.append(hashlib.sha1(buf).hexdigest())

        f.close()

        self.md5 = m.hexdigest()
        self.sha1 = s1.hexdigest()
        self.sha256 = s256.hexdigest()

    def dump_raw(self):
        r = []
        for i in self.pieces:
            r.append('piece %s' % i)
        r.append('md5 %s' % self.md5)
        r.append('sha1 %s' % self.sha1)
        if sha256:
            r.append('sha256 %s' % self.sha256)
        return '\n'.join(r)


    def __str__(self):
        return self.dump_raw()


    def dump_2_12_template(self):
        """dump in the form that was used up to mirrorbrain-2.12.0"""

        r = []


        r.append("""      <verification>
        <hash type="md5">%s</hash>
        <hash type="sha1">%s</hash>""" % (self.md5, self.sha1))
        if self.sha256:
            r.append('        <hash type="sha256">%s</hash>' % (self.sha256))

        if self.pgp:
            r.append('        <signature type="pgp" file="%s.asc">' % self.basename)
            r.append(self.pgp)
            r.append('        </signature>')

        r.append('        <pieces length="%s" type="sha1">' % (PIECESIZE))

        n = 0
        for piece in self.pieces:
            r.append('            <hash piece="%s">%s</hash>' % (n, piece))
            n += 1

        r.append('        </pieces>\n      </verification>\n')

        return '\n'.join(r)


