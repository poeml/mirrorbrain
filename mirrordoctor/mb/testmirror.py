
import os
import sys
import urllib2
import commands
import tempfile
import shutil
import socket
import mb.util

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


def req(baseurl, filename, http_method='GET', do_digest=False):

    url = baseurl + filename
    worked = False
    digest = None

    if url.startswith('http://') or url.startswith('ftp://'):
        req = urllib2.Request(url)
        if url.startswith('http://') and http_method=='HEAD':
            # not for FTP URLs
            req.get_method = lambda: 'HEAD'

        try:
            response = urllib2.urlopen(req)
            worked = True
        except KeyboardInterrupt:
            print >>sys.stderr, 'interrupted!'
            raise
        except:
            return (0, digest)

        if do_digest:
            try:
                t = tempfile.NamedTemporaryFile()
                while 1:
                    buf = response.read(1024*512)
                    if not buf: break
                    t.write(buf)
                t.flush()
                digest = mb.util.dgst(t.name)
                t.close()
            except:
                return (0, digest)

        if url.startswith('http://'):
            rc = response.code
        elif url.startswith('ftp://'):
            out = response.readline()
            if len(out):
                rc = 1
            else:
                rc = 0

        return (rc, digest)

    elif url.startswith('rsync://'):

        try:
            tmpdir = tempfile.mkdtemp(prefix='mb_probefile_')
            # note the -r; *some* option is needed because many rsync servers
            # don't reply properly if they don't get any option at all.
            # -t (as the most harmless option) also isn't sufficient.
            #
            # replaced -r with -d, because it allows to probe for directories
            # without transferring them recursively. With 92 mirrors tested, it
            # worked just as well, with a single exception. (ftp3.gwdg.de, which 
            # presumabely runs a really old rsync server. The system seems to be 
            # SuSE Linux 8.2.)
            # poeml, Mon Jun 22 18:10:33 CEST 2009
            cmd = 'rsync -d --timeout=%d %s %s/' % (TIMEOUT, url, tmpdir)
            (rc, out) = commands.getstatusoutput(cmd)
            targetfile = os.path.join(tmpdir, os.path.basename(filename))
            worked = os.path.exists(targetfile)
            if worked and do_digest:
                digest = mb.util.dgst(targetfile)


        finally:
            shutil.rmtree(tmpdir, ignore_errors=True)

        if rc != 0:
            return (0, digest)

        if worked:
            return (200, digest)
        else:
            return (0, digest)

    else:
        raise 'unknown URL type: %r' % baseurl

