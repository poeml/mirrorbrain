
import os
import sys
import urllib2

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
        import commands

        cmd = 'rsync %s' % url
        (rc, out) = commands.getstatusoutput(cmd)

        # look only at the last line
        s = out.splitlines()[-1]
        # look only at the last word
        try:
            s = s.split()[-1]
        except:
            s = ''
        try:
            s2 = url.split('/')[-1]
        except:
            s2 = ''

        if rc != 0:
            return 1
        if s == s2:
            return 200
        else:
            return 0

    else:
        raise 'unknown URL type: %r' % baseurl

