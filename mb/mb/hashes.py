import sys
import os
import os.path
import stat
import binascii

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

DEFAULT_PIECESIZE = 262144
MD5_DIGESTSIZE = 16
SHA1_DIGESTSIZE = 20
SHA256_DIGESTSIZE = 32


class Hasheable:
    """represent a file and its metadata"""

    def __init__(self, basename, src_dir=None, dst_dir=None,
                 base_dir=None, do_zsync_hashes=False,
                 do_chunked_hashes=True, chunk_size=DEFAULT_PIECESIZE, do_chunked_with_zsync=False):
        self.basename = basename
        if src_dir:
            self.src_dir = src_dir
        else:
            self.src_dir = os.path.dirname(self.basename)

        self.src = os.path.join(src_dir, self.basename)
        self.base_dir = base_dir
        self.src_rel = os.path.join(
            src_dir[len(base_dir):], self.basename).lstrip('/')

        self.finfo = os.lstat(self.src)
        self.atime = self.finfo.st_atime
        self.mtime = self.finfo.st_mtime
        self.size = self.finfo.st_size
        self.inode = self.finfo.st_ino
        self.mode = self.finfo.st_mode

        self.dst_dir = dst_dir

        self.dst_basename = '%s.size_%s' % (self.basename, self.size)
        self.dst = os.path.join(self.dst_dir, self.dst_basename)

        self.hb = HashBag(src=self.src, parent=self)
        self.hb.do_zsync_hashes = do_zsync_hashes
        self.hb.do_chunked_hashes = do_chunked_hashes
        self.hb.do_chunked_with_zsync = do_chunked_with_zsync
        self.hb.chunk_size = chunk_size

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
        except OSError:
            dst_mtime = 0  # file missing

        if int(dst_mtime) == int(self.mtime) and not force:
            if verbose:
                print('Up to date hash file: %r' % self.dst)
            return

        if dry_run:
            print('Would create hash file', self.dst)
            return

        if self.hb.empty:
            self.hb.fill(verbose=verbose)
        if self.hb.empty:
            sys.stderr.write('skipping hash (file) generation\n')
            return

        open(self.dst, 'w').close()

        if verbose:
            print('Hash file updated: %r' % self.dst)

        os.utime(self.dst, (self.atime, self.mtime))

        if copy_permissions:
            os.chmod(self.dst, self.mode)
        else:
            os.chmod(self.dst, 0o0644)

    def check_db(self, conn, verbose=False, dry_run=False, force=False):
        """check if the hashes that are stored in the database are up to date

        for performance, this function talks very low level to the database"""
        # get a database cursor, but make it persistent which is faster
        try:
            conn.mycursor
        except AttributeError:
            conn.mycursor = conn.Hash._connection.getConnection().cursor()
        c = conn.mycursor

        if self.hb.chunk_size == 0 or (self.size + self.hb.chunk_size - 1) / (self.hb.chunk_size - 1) != len(self.hb.pieceshex):
            self.hb.zsyncpieceshex = []

        c.execute("SELECT id FROM filearr WHERE path = %s LIMIT 1",
                  [self.src_rel])
        res_filearr = c.fetchone()
        if res_filearr:
            # file already present in the file array table. Is it also known in the hash table?
            file_id = res_filearr[0]
            c.execute("SELECT file_id, mtime, size FROM hash WHERE file_id = %s LIMIT 1",
                      [file_id])
            res_hash = c.fetchone()
        else:
            print('File %r not in database. Not on mirrors yet? Will be inserted.' % self.src_rel)
            file_id = None
            res_hash = None

        if not res_hash:

            if dry_run:
                print('Would create hashes in db for: ', self.src_rel)
                return

            if self.hb.empty:
                self.hb.fill(verbose=verbose)
            if self.hb.empty:
                sys.stderr.write('skipping db hash generation\n')
                return

            c.execute("BEGIN")
            if not res_filearr:
                c.execute("INSERT INTO filearr (path, mirrors) VALUES (%s, '{}')",
                          [self.src_rel])
                c.execute("SELECT currval('filearr_id_seq')")
                file_id = c.fetchone()[0]
            zsums = ''
            for i in self.hb.zsums:
                zsums = zsums + i.hexdigest()

            c.execute("""INSERT INTO hash (file_id, mtime, size, md5, 
                                           sha1, sha256, sha1piecesize, 
                                           sha1pieces, btih, pgp, zblocksize,
                                           zhashlens, zsums) 
                         VALUES (%s, %s, %s, 
                                 decode(%s, 'hex'), decode(%s, 'hex'), 
                                 decode(%s, 'hex'), %s, decode(%s, 'hex'),
                                 decode(%s, 'hex'),
                                 %s, %s, %s, decode(%s, 'hex'))""",
                      [file_id, int(self.mtime), self.size,
                       self.hb.md5hex or '',
                       self.hb.sha1hex or '',
                       self.hb.sha256hex or '',
                       self.hb.chunk_size,
                       ''.join(self.hb.pieceshex) +
                       ''.join(self.hb.zsyncpieceshex),
                       self.hb.btihhex or '',
                       self.hb.pgp or '',
                       self.hb.zblocksize,
                       self.hb.get_zparams(),
                       zsums]
                      )
            c.execute("COMMIT")
            if verbose:
                print('Hash was not present yet in database - inserted')
        else:
            mtime, size = res_hash[1], res_hash[2]

            if int(self.mtime) == mtime and self.size == size and not force:
                if verbose:
                    print('Up to date in db: %r' % self.src_rel)
                return

            if self.hb.empty:
                self.hb.fill(verbose=verbose)
            if self.hb.empty:
                sys.stderr.write('skipping db hash generation\n')
                return

            zsums = ''
            for i in self.hb.zsums:
                zsums = zsums + i.hexdigest()

            c.execute("""UPDATE hash set mtime = %s, size = %s, 
                                         md5 = decode(%s, 'hex'), 
                                         sha1 = decode(%s, 'hex'), 
                                         sha256 = decode(%s, 'hex'), 
                                         sha1piecesize = %s,
                                         sha1pieces = decode(%s, 'hex'), 
                                         btih = decode(%s, 'hex'),
                                         pgp = %s,
                                         zblocksize = %s,
                                         zhashlens = %s,
                                         zsums = decode(%s, 'hex')
                         WHERE file_id = %s""",
                      [int(self.mtime), self.size,
                       self.hb.md5hex or '', self.hb.sha1hex or '', self.hb.sha256hex or '',
                       self.hb.chunk_size, ''.join(
                           self.hb.pieceshex) + ''.join(self.hb.zsyncpieceshex),
                       self.hb.btihhex or '',
                       self.hb.pgp or '',
                       self.hb.zblocksize,
                       self.hb.get_zparams(),
                       zsums,
                       file_id])
            if verbose:
                print('Hash updated in database for %r' % self.src_rel)

        c.execute('commit')

        self.hb = None

    def __str__(self):
        return self.basename


class HashBag:

    def __init__(self, src, parent=None):
        self.src = src
        self.basename = os.path.basename(src)
        self.h = parent

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
        self.zsyncpieceshex = []
        self.btih = None
        self.btihhex = None

        self.do_zsync_hashes = False
        self.zsums = []
        self.zblocksize = 0
        self.zseq_matches = None
        self.zrsum_len = None
        self.zchecksum_len = None

        self.empty = True

    def fill(self, verbose=False):
        verbose = True  # XXX
        if verbose:
            sys.stdout.write('Hashing %r... ' % self.src)
            sys.stdout.flush()
        if self.do_chunked_with_zsync or self.do_zsync_hashes:
            import zsync

        if self.do_zsync_hashes:
            self.zs_guess_zsync_params()

        m = md5.md5()
        s1 = sha1.sha1()
        if sha256:
            s256 = sha256.sha256()
        short_read_before = False

        try:
            f = open(self.src, 'rb')
        except IOError as e:
            sys.stderr.write('%s\n' % e)
            return None

        while 1 + 1 == 2:
            buf = f.read(self.chunk_size)
            if not buf:
                break

            if len(buf) != self.chunk_size:
                if not short_read_before:
                    short_read_before = True
                else:
                    raise('InternalError')

            m.update(buf)
            s1.update(buf)
            if sha256:
                s256.update(buf)

            self.npieces += 1
            if self.do_chunked_hashes:
                self.pieces.append(sha1.sha1(buf).digest())
                self.pieceshex.append(sha1.sha1(buf).hexdigest())
                if self.do_chunked_with_zsync:
                    self.zsyncpieceshex.append(
                        self.get_zsync_digest(buf, self.chunk_size))

            if self.do_zsync_hashes:
                self.zs_get_block_sums(buf)

        f.close()

        self.md5 = m.digest()
        self.md5hex = m.hexdigest()
        self.sha1 = s1.digest()
        self.sha1hex = s1.hexdigest()
        if sha256:
            self.sha256 = s256.digest()
            self.sha256hex = s256.hexdigest()

        if self.do_chunked_hashes:
            self.calc_btih()

        # if present, grab PGP signature
        # but not if the signature file is larger than
        # the actual file -- that would be a sign that the signature
        # is not "detached", and could be huge (or contain characters that
        # can not easily be saved to the database for encoding reasons)
        if os.path.exists(self.src + '.asc') \
                and os.stat(self.src + '.asc').st_size < self.h.size:
            self.pgp = open(self.src + '.asc').read()

        # print (len(self.zsums))

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
        r.append('btih %s' % self.btihhex)
        return '\n'.join(r)

    def get_zparams(self):
        if self.zseq_matches and \
           self.zrsum_len and \
           self.zchecksum_len:
            return '%s,%s,%s' % (self.zseq_matches,
                                 self.zrsum_len,
                                 self.zchecksum_len)
        else:
            return ''

    def __str__(self):
        return self.dump_raw()

    def zs_guess_zsync_params(self):
        import math

        size = self.h.size
        if size < 100000000:
            blocksize = 2048
        else:
            blocksize = 4096

        # Decide how long a rsum hash and checksum hash per block we need for this file
        if size > blocksize:
            seq_matches = 2
        else:
            seq_matches = 1

        rsum_len = math.ceil(
            ((math.log(size or 1) + math.log(blocksize)) / math.log(2) - 8.6) / seq_matches / 8)

        # min and max lengths of rsums to store
        if rsum_len > 4:
            rsum_len = 4
        if rsum_len < 2:
            rsum_len = 2

        # Now the checksum length; min of two calculations
        checksum_len = math.ceil(
            (20 + (math.log(size or 1) + math.log(1 + size / blocksize)) / math.log(2))
            / seq_matches / 8)
        checksum_len2 = (
            7.9 + (20 + math.log(1 + size / blocksize) / math.log(2))) / 8

        if checksum_len < checksum_len2:
            checksum_len = checksum_len2

        self.zblocksize = blocksize
        self.zseq_matches = seq_matches
        self.zrsum_len = int(rsum_len)
        self.zchecksum_len = int(checksum_len)

        # print ('%s: %s,%s,%s' % (self.zblocksize, self.zseq_matches, self.zrsum_len, self.zchecksum_len))

    def zs_get_block_sums(self, buf):

        offset = 0
        while 1:
            block = buf[offset: offset + self.zblocksize]
            offset += self.zblocksize
            if not block:
                # print ('last.')
                break

            # padding
            if len(block) < self.zblocksize:
                block = block + ('\x00' * (self.zblocksize - len(block)))

            md4 = hashlib.new('md4')
            md4.update(block)
            c = md4.digest()

            if self.do_zsync_hashes:
                r = zsync.rsum06(block)

                # save only some trailing bytes
                self.zsums.append(r[-self.zrsum_len:])
                # save only some leading bytes
                self.zsums.append(c[0:self.zchecksum_len])

    def get_zsync_digest(self, buf, blocksize):
        if len(buf) < blocksize:
            buf = buf + ('\x00' * (blocksize - len(buf)))
        r = zsync.rsum06(buf)
        return "%02x%02x%02x%02x" % (ord(r[3]), ord(r[2]), ord(r[1]), ord(r[0]))

    def calc_btih(self):
        """ calculate a bittorrent information hash (btih) """
        size = 0
        if self.h:
            size = self.h.size

        buf = [b'd',
               b'6:length', b'i', str(size).encode(), b'e',
               b'6:md5sum', str(MD5_DIGESTSIZE * 2).encode(), b':', self.md5hex.encode(),
               b'4:name', str(len(self.basename)).encode(), b':', self.basename.encode(),
               b'12:piece length', b'i', str(self.chunk_size).encode(), b'e',
               b'6:pieces', str(len(self.pieces) *
                                SHA1_DIGESTSIZE).encode(), b':', b''.join(self.pieces),
               b'4:sha1', str(SHA1_DIGESTSIZE).encode(), b':', self.sha1,
               b'6:sha256', str(SHA256_DIGESTSIZE).encode(), b':', self.sha256 or b'',
               b'e']

        h = sha1.sha1()
        h.update(b''.join(buf))
        self.btih = h.digest()
        self.btihhex = h.hexdigest()
