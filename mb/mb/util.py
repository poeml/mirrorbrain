import geoip2.database

import sys
import os
import time
import socket

t_start = 0
rsync_version = None

class VersionParser:
    def __init__(self, vers):
        self.vers = vers
        s = vers.split('.')
        self.major = int(s[0])
        self.minor = int(s[1])
        self.patchlevel = int(s[2])

    def __str__(self):
        return self.vers


class Afile:
    """represent a file, found during scanning"""

    def __init__(self, name, size, mtime=0, path=0):
        self.name = name
        self.size = int(size)
        self.mtime = mtime

        self.path = path

    def __str__(self):
        return self.name


class MirrorBrainHost:
    """represent an IP address, or rather some data associated with it"""

    def __init__(self, address, maxmind_asn_db = '/var/lib/GeoIP/GeoLite2-ASN.mmdb', maxmind_city_db = '/var/lib/GeoIP/GeoLite2-City.mmdb'):
        self.address = address
        self.ip = None
        self.ip6 = None
        self.asn = None
        self.asn6 = None
        self.prefix = None
        self.prefix6 = None
        self.city_info = None

        self.maxmind_asn_db = maxmind_asn_db
        self.maxmind_city_db = maxmind_city_db

        self._resolv_address(address)
        self._find_city_info()
        self._find_asn()

    def country_code(self):
        return self.city_info.country.iso_code.lower()


    def region_code(self):
        return self.city_info.continent.code.lower()


    def coordinates(self):
        lat = round(self.city_info.location.latitude, 3)
        lng = round(self.city_info.location.longitude, 3)
        return lat, lng

    def ipv6Only(self):
        if self.ip6 and not self.ip:
            return True
        else:
            return False

    def __str__(self):
        r = []
        if self.ip:
            r.append('%s (%s AS%s)' % (self.ip, self.prefix, self.asn))
        if self.ip6:
            r.append('%s (%s AS%s)' % (self.ip6, self.prefix6, self.asn6))
        return ' '.join(r)

    def _find_city_info(self):
        self.city_db   = geoip2.database.Reader(self.maxmind_city_db)
        if self.ip:
            try:
                self.city_info = self.city_db.city(self.ip)
            except geoip2.errors.AddressNotFoundError:
                # we get this error if mod_asn isn't installed as well
                pass

        if self.ip6:
            try:
                self.city_info = self.city_db.city(self.ip6)
            except geoip2.errors.AddressNotFoundError:
                # we get this error if mod_asn isn't installed as well
                pass

    def _find_asn(self):
        # TODO: maxmindcode here
        self.asn_db    = geoip2.database.Reader(self.maxmind_asn_db)

        if self.ip:
            try:
                res = self.asn_db.asn(self.ip)
                self.prefix = res.network
                self.asn    = res.autonomous_system_number
            except geoip2.errors.AddressNotFoundError:
                # we get this error if mod_asn isn't installed as well
                pass

        if self.ip6:
            try:
                res = self.asn_db.asn(self.ip6)
                self.prefix6 = res.network
                self.asn6    = res.autonomous_system_number
            except geoip2.errors.AddressNotFoundError:
                # we get this error if mod_asn isn't installed as well
                pass

    def _resolv_address(self, s):
        ips = []
        ip6s = []
        try:
            for res in socket.getaddrinfo(s, None):
                af, socktype, proto, canonname, sa = res
                if af == socket.AF_INET6:
                    if sa[0] not in ip6s:
                        ip6s.append(sa[0])
                else:
                    if sa[0] not in ips:
                        ips.append(sa[0])
        except socket.error as e:
            if e[0] == socket.EAI_NONAME:
                raise mb.mberr.NameOrServiceNotKnown(s)
            else:
                print ('socket error msg:', str(e))
                return None

        # print (ips)
        # print (ip6s)
        if len(ips) > 1 or len(ip6s) > 1:
            print ('>>> warning: %r resolves to multiple IP addresses: ' % s, file=sys.stderr)
            if len(ips) > 1:
                print (', '.join(ips), file=sys.stderr)
            if len(ip6s) > 1:
                print (', '.join(ip6s), file=sys.stderr)
            print ('\n>>> see http://mirrorbrain.org/archive/mirrorbrain/0042.html why this could\n' \
                  '>>> could be a problem, and what to do about it. But note that this is not\n' \
                  '>>> necessarily a problem and could actually be intended depending on the\n' \
                  '>>> mirror\'s configuration (see http://mirrorbrain.org/issues/issue152).\n' \
                  '>>> It\'s best to talk to the mirror\'s admins.\n', file=sys.stderr)
        if ips:
            self.ip = ips[0]
        if ip6s:
            self.ip6 = ip6s[0]

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

        self.probeurl = self.probebaseurl.rstrip(
            '/') + '/' + self.filename.lstrip('/')

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
    import os
    import base64

    image = open(os.path.join(basedir, path)).read()
    data = base64.standard_b64encode(image)
    ext = os.path.splitext(path)[1]

    return 'data:image/%s;base64,%s' % (ext, data)


def hostname_from_url(url):
    from urllib.parse import urlparse
    h = urlparse(url)[1]
    if ':' in h:
        h = h.split(':')[0]
    return h


def af_from_string(address):
    s=str(address)
    right = s.find('/')
    if right < 0:
        right = len(s)
    return socket.getaddrinfo(s[:right], 0)[0][0]


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
        if not buf:
            break
        md5_hash.update(buf)
    f.close()
    return md5_hash.hexdigest()


def edit_file(data, boilerplate=None):
    import tempfile
    import difflib

    #delim = '--This line, and those below, will be ignored--\n\n'
    if boilerplate:
        data = boilerplate + data

    (fd, filename) = tempfile.mkstemp(
        prefix='mb-editmirror', suffix='.txt', dir='/tmp')
    f = os.fdopen(fd, 'w')
    f.write(data)
    # f.write('\n')
    # f.write(delim)
    f.close()
    hash_orig = dgst(filename)

    editor = os.getenv('EDITOR', os.getenv('VISUAL', default='vim'))
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
            d = [line for line in d if not line.startswith('?')]
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

    from urllib import urlsplit, urlunsplit

    u = urlsplit(s)
    netloc = u[1]
    if '@' in netloc:
        netloc = netloc.split('@')[1]
    return urlunsplit((u[0], netloc, u[2], u[3], u[4]))


def pgsql_regexp_esc(s):
    if s:
        return '\\\\' + '\\\\'.join(['%03o' % ord(c) for c in s])
    else:
        return s


if __name__ == '__main__':
    import sys
    mbgeoip = MirrorBrainHost(sys.argv[1])
    print ('country:',     mbgeoip.country_code())
    print ('region:',      mbgeoip.region_code())
    print ('coordinates:', mbgeoip.coordinates())
