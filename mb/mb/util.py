import sys, os
import time

t_start = 0
rsync_version = None


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


class Sample:
    """used for probe results."""
    def __init__(self, identifier, probebaseurl, filename, 
                 get_digest=False, get_content=False):
        self.identifier = identifier
        self.probebaseurl = probebaseurl
        self.filename = filename
        self.has_file = False
        self.http_code = None
        self.get_digest = get_digest
        self.digest = None
        self.get_content = get_content
        self.content = None

        if self.probebaseurl.startswith('http://'):
            self.scheme = 'http'
        elif self.probebaseurl.startswith('ftp://'): 
            self.scheme = 'ftp'
        elif self.probebaseurl.startswith('rsync://') \
                or ('://' not in self.probebaseurl and '::' in self.probebaseurl):
            self.scheme = 'rsync'
        else:
            raise Exception('unknown url type: %s' % self.probebaseurl)

        self.probeurl = self.probebaseurl.rstrip('/') + '/' + self.filename.lstrip('/')

        # checksumming content implies downloading it
        if self.get_digest:
            self.get_content = True


    def __str__(self):
        s = 'M: %s %s, has_file=%s' \
                % (self.identifier, self.probeurl, self.has_file)
        if self.http_code:
            s += ', http_code=%s' % self.http_code
        if self.digest:
            s += ', digest=%s' % self.digest
        return s


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


def get_rsync_version():
    """call the rsync program and get to know its version number.  Save the
    result in a global, and returned only the "cached" result in subsequent
    calls."""

    global rsync_version

    if rsync_version:
        return rsync_version
    else:
        import commands
        status, output = commands.getstatusoutput('rsync --version')
        if status != 0:
            sys.exit('rsync command not found')
        rsync_version = output.splitlines()[0].split()[2]
        return rsync_version


def timer_start():
    global t_start
    t_start = time.time()


def timer_elapsed():
    global t_start

    t_end = time.time()
    t_delta = t_end - t_start
    if t_delta > 60 * 60: 
        return '%s hours' % round((t_delta / 60 / 60), 1)
    elif t_delta > 60:
        return '%s minutes' % round((t_delta / 60), 1)
    else:
        return '%s seconds' % int(t_delta)


def strip_auth(s):
    """remove user/password from URLs. The URL is split into
    <scheme>://<netloc>/<path>;<params>?<query>#<fragment>
    with the urlparse module and and returned reassembled.
    """

    import urlparse

    u = urlparse.urlsplit(s)
    netloc = u[1]
    if '@' in netloc:
        netloc = netloc.split('@')[1]
    return urlparse.urlunsplit((u[0], netloc, u[2], u[3], u[4]))

