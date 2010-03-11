#!/usr/bin/python

import sys
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
    import sha
    sha1 = sha
    sha1.sha1 = sha1.sha
    # I think Python 2.4 didn't have a sha256 counterpart
    sha256 = None

PIECESIZE = 262144


class Hasheable:
    """represent a file and its metadata"""
    def __init__(self, basename, src_dir=None, dst_dir=None,
                 base_dir=None):
        self.basename = basename
        if src_dir:
            self.src_dir = src_dir
        else:
            self.src_dir = os.path.dirname(self.basename)

        self.src = os.path.join(src_dir, self.basename)
        self.base_dir = base_dir
        self.src_rel = os.path.join(src_dir[len(base_dir):], self.basename).lstrip('/')

        self.finfo = os.lstat(self.src)
        self.atime = self.finfo.st_atime
        self.mtime = self.finfo.st_mtime
        self.size  = self.finfo.st_size
        self.inode = self.finfo.st_ino
        self.mode  = self.finfo.st_mode

        self.dst_dir = dst_dir

        self.dst_basename = '%s.size_%s' % (self.basename, self.size)
        self.dst = os.path.join(self.dst_dir, self.dst_basename)

        self.hb = HashBag(src = self.src)

    def islink(self):
        return stat.S_ISLNK(self.mode)
    def isreg(self):
        return stat.S_ISREG(self.mode)
    def isdir(self):
        return stat.S_ISDIR(self.mode)


    def check_file(self, verbose=False, dry_run=False, force=False, copy_permissions=True):
        """check whether the hashes stored on disk are up to date"""
        try:
            dst_statinfo = os.stat(self.dst)
            dst_mtime = dst_statinfo.st_mtime
            dst_size = dst_statinfo.st_size
        except OSError:
            dst_mtime = dst_size = 0 # file missing

        if int(dst_mtime) == int(self.mtime) and dst_size != 0 and not force:
            if verbose:
                print 'Up to date hash file: %r' % self.dst
            return 

        if dry_run: 
            print 'Would make hash file', self.dst
            return

        if self.hb.empty:
            self.hb.fill(verbose=verbose)

        d = open(self.dst, 'wb')
        d.write(self.hb.dump_2_12_template())
        d.close()

        if verbose:
            print 'Hash file updated: %r' % self.dst

        os.utime(self.dst, (self.atime, self.mtime))

        if copy_permissions:
            os.chmod(self.dst, self.mode)
        else:
            os.chmod(self.dst, 0644)


    def check_db(self, conn, verbose=False, dry_run=False, force=False):
        """check if the hashes that are stored in the database are up to date
        
        for performance, this function talks very low level to the database"""
        # get a database cursor, but make it persistent which is faster
        try:
            conn.mycursor
        except AttributeError:
            conn.mycursor = conn.Hash._connection.getConnection().cursor()
        c = conn.mycursor

        c.execute("SELECT id FROM filearr WHERE path = %s LIMIT 1",
                  [self.src_rel])
        res = c.fetchone()
        if res:
            file_id = res[0]
        else:
            print 'File %r not found. Not on mirrors yet? Inserting.' % self.src_rel
            c.execute("INSERT INTO filearr (path, mirrors) VALUES (%s, '{}')",
                      [self.src_rel])
            c.execute("SELECT currval('filearr_id_seq')")
            file_id =  c.fetchone()[0]
            c.execute("commit")
            

        c.execute("SELECT file_id, mtime, size FROM hash WHERE file_id = %s LIMIT 1",
                  [file_id])
        res = c.fetchone()

        if not res:

            if dry_run: 
                print 'Would create hashes in db for: ', self.src_rel
                return

            if self.hb.empty:
                self.hb.fill(verbose=verbose)

            c.execute("""INSERT INTO hash (file_id, mtime, size, md5, 
                                           sha1, sha256, sha1piecesize, 
                                           sha1pieces, pgp) 
                         VALUES (%s, %s, %s, 
                                 decode(%s, 'hex'), decode(%s, 'hex'), 
                                 decode(%s, 'hex'), %s, decode(%s, 'hex'),
                                 %s )""",
                      [file_id, int(self.mtime), self.size,
                       self.hb.md5hex,
                       self.hb.sha1hex,
                       self.hb.sha256hex or '',
                       PIECESIZE,
                       ''.join(self.hb.pieceshex),
                       self.hb.pgp or ''])
            if verbose:
                print 'Hash was not present yet in database - inserted'
        else:
            mtime, size = res[1], res[2]
            
            if int(self.mtime) == mtime and self.size == size and not force:
                if verbose:
                    print 'Up to date in db: %r' % self.src_rel
                return

            if self.hb.empty:
                self.hb.fill(verbose=verbose)

            c.execute("""UPDATE hash set mtime = %s, size = %s, 
                                         md5 = decode(%s, 'hex'), 
                                         sha1 = decode(%s, 'hex'), 
                                         sha256 = decode(%s, 'hex'), 
                                         sha1piecesize = %s,
                                         sha1pieces = decode(%s, 'hex'), 
                                         pgp = %s
                         WHERE file_id = %s""",
                      [int(self.mtime), self.size,
                       self.hb.md5hex, self.hb.sha1hex, self.hb.sha256hex or '',
                       PIECESIZE, ''.join(self.hb.pieceshex),
                       self.hb.pgp or '', 
                       file_id])
            if verbose:
                print 'Hash updated in database for %r' % self.src_rel

        c.execute('commit')



    #def __eq__(self, other):
    #    return self.basename == other.basename
    #def __eq__(self, basename):
    #    return self.basename == basename
        
    def __str__(self):
        return self.basename



class HashBag():

    def __init__(self, src):
        self.src = src
        self.basename = os.path.basename(src)

        self.md5 = None
        self.sha1 = None
        self.sha256 = None
        self.md5hex = None
        self.sha1hex = None
        self.sha256hex = None
        self.pgp = None

        self.npieces = 0
        self.pieces = []
        self.pieceshex = []

        self.empty = True

    def fill(self, verbose=False):
        verbose = True # XXX
        if verbose:
            sys.stdout.write('Hashing %r... ' % self.src)
            sys.stdout.flush()

        m = md5.md5()
        s1 = sha1.sha1()
        if sha256:
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
            if sha256:
                s256.update(buf)

            self.npieces += 1
            self.pieces.append(sha1.sha1(buf).digest())
            self.pieceshex.append(sha1.sha1(buf).hexdigest())

        f.close()

        self.md5 = m.digest()
        self.md5hex = m.hexdigest()
        self.sha1 = s1.digest()
        self.sha1hex = s1.hexdigest()
        if sha256:
            self.sha256 = s256.digest()
            self.sha256hex = s256.hexdigest()

        # if present, grab PGP signature
        if os.path.exists(self.src + '.asc'):
            self.pgp = open(self.src + '.asc').read()

        self.empty = False

        if verbose:
            sys.stdout.write('done.\n')


    def dump_raw(self):
        r = []
        for i in self.pieceshex:
            r.append('piece %s' % i)
        r.append('md5 %s' % self.md5hex)
        r.append('sha1 %s' % self.sha1hex)
        if sha256:
            r.append('sha256 %s' % self.sha256hex)
        return '\n'.join(r)


    def __str__(self):
        return self.dump_raw()


    def dump_2_12_template(self):
        """dump in the form that was used up to mirrorbrain-2.12.0"""

        r = []


        r.append("""      <verification>
        <hash type="md5">%s</hash>
        <hash type="sha1">%s</hash>""" % (self.md5hex, self.sha1hex))
        if self.sha256:
            r.append('        <hash type="sha256">%s</hash>' % (self.sha256hex))

        if self.pgp:
            r.append('        <signature type="pgp" file="%s.asc">' % self.basename)
            r.append(self.pgp)
            r.append('        </signature>')

        r.append('        <pieces length="%s" type="sha1">' % (PIECESIZE))

        n = 0
        for piece in self.pieceshex:
            r.append('            <hash piece="%s">%s</hash>' % (n, piece))
            n += 1

        r.append('        </pieces>\n      </verification>\n')

        return '\n'.join(r)


