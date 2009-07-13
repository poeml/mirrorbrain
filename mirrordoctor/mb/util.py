import sys, os


class Afile:
    """represent a file, found during scanning"""
    def __init__(self, name, size, mtime=0, path=0):
        self.name = name
        self.size = int(size)
        self.mtime = mtime

        self.path = path
    def __str__(self):
        return self.name


class IpAddress:
    """represent an IP address, or rather some data associated with it"""
    def __init__(self, ip):
        self.ip = ip
        self.asn = None
        self.prefix = None
    def __str__(self):
        return '%s (%s AS%s)' % (self.ip, self.prefix, self.asn)


def data_url(basedir, path):
    import os, base64

    image = open(os.path.join(basedir, path)).read()
    data = base64.standard_b64encode(image)
    ext = os.path.splitext(path)[1]

    return 'data:image/%s;base64,%s' % (ext, data)


def hostname_from_url(url):
    import urlparse
    h = urlparse.urlparse(url)[1]
    if ':' in h:
        h = h.split(':')[0]
    return h


def dgst(file):
    # Python 2.5 depracates the md5 modules
    # Python 2.4 doesn't have hashlib yet
    try:
        import hashlib
        md5_hash = hashlib.md5()
    except ImportError:
        import md5
        md5_hash = md5.new()

    BUFSIZE = 1024*1024
    f = open(file, 'r')
    while 1:
        buf = f.read(BUFSIZE)
        if not buf: break
        md5_hash.update(buf)
    f.close()
    return md5_hash.hexdigest()


def edit_file(data, boilerplate = None):
    import tempfile, difflib

    #delim = '--This line, and those below, will be ignored--\n\n'
    if boilerplate:
        data = boilerplate + data

    (fd, filename) = tempfile.mkstemp(prefix = 'mb-editmirror', suffix = '.txt', dir = '/tmp')
    f = os.fdopen(fd, 'w')
    f.write(data)
    #f.write('\n')
    #f.write(delim)
    f.close()
    hash_orig = dgst(filename)

    editor = os.getenv('EDITOR', default='vim')
    while 1:
        os.system('%s %s' % (editor, filename))
        hash = dgst(filename)

        if hash == hash_orig:
            sys.stdout.write('No changes.\n')
            os.unlink(filename)
            return 
        else:
            new = open(filename).read()
            #new = new.split(delim)[0].rstrip()

            differ = difflib.Differ()
            d = list(differ.compare(data.splitlines(1), new.splitlines(1)))
            d = [ line for line in d if not line.startswith('?') ] 
            sys.stdout.writelines(d)
            sys.stdout.write('\n\n')

            input = raw_input('Save changes?\n'
                              'y)es, n)o, e)dit again: ')
            if input in 'yY':
                os.unlink(filename)
                return new
            elif input in 'nN':
                os.unlink(filename)
                return
            else:
                pass
