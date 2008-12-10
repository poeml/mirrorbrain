
import os
import sys
import urllib2
import commands
import tempfile
import shutil
import socket

TIMEOUT = 20

socket.setdefaulttimeout(TIMEOUT)

def access_http(url):
    r = urllib2.urlopen(url).read()
    print r


def dont_use_proxies():
    # an "are you alive check" is relatively useless if it is answered by an intermediate cache
    for i in ['http_proxy', 'HTTP_PROXY', 'ftp_proxy', 'FTP_PROXY']:
        if i in os.environ:
            del os.environ[i]


def req(baseurl, filename, http_method='GET'):

    url = baseurl + filename

    if url.startswith('http://') or url.startswith('ftp://'):
        req = urllib2.Request(url)
        if url.startswith('http://') and http_method=='HEAD':
            # not for FTP URLs
            req.get_method = lambda: 'HEAD'

        try:
            response = urllib2.urlopen(req)
            return response.code
        except KeyboardInterrupt:
            print >>sys.stderr, 'interrupted!'
            raise
        except:
            return 0

    elif url.startswith('rsync://'):

        worked = False
        try:
            tmpdir = tempfile.mkdtemp(prefix='mb_probefile_')
            # note the -r; *some* option is needed because many rsync servers
            # don't reply properly if they don't get any option at all.
            # -t (as the most harmless option) also isn't sufficient.
            cmd = 'rsync -r --timeout=%d %s %s/' % (TIMEOUT, url, tmpdir)
            (rc, out) = commands.getstatusoutput(cmd)
            worked = os.path.exists(os.path.join(tmpdir, os.path.basename(filename)))

        finally:
            shutil.rmtree(tmpdir, ignore_errors=True)

        if rc != 0:
            return 1

        if worked:
            return 200
        else:
            return 0

    else:
        raise 'unknown URL type: %r' % baseurl

