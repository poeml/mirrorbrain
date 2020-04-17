import os
import sys
import urllib.request
import subprocess
import tempfile
import shutil
import socket
import mb.util

TIMEOUT = 20

socket.setdefaulttimeout(TIMEOUT)


def access_http(identifier, url):
    from mb.util import Sample
    S = Sample(identifier, url, '', get_content=True)
    probe(S)
    return S


def dont_use_proxies():
    # an "are you alive check" is relatively useless if it is answered by an intermediate cache
    for i in ['http_proxy', 'HTTP_PROXY', 'ftp_proxy', 'FTP_PROXY']:
        if i in os.environ:
            del os.environ[i]


def req(baseurl, filename, http_method='GET', get_digest=False):
    """compatibility method that wraps around probe(). It was used
    before probe() existed and is probably not needed anymore.
    """
    from mb.util import Sample
    S = Sample('', baseurl, filename, get_digest=get_digest)
    probe(S, http_method=http_method)
    return (S.http_code, S.digest)


def probe(S, http_method='GET'):

    if S.scheme in ['http', 'ftp']:
        req = urllib.request.Request(S.probeurl)
        if S.scheme == 'http' and http_method == 'HEAD':
            # not for FTP URLs
            req.get_method = lambda: 'HEAD'

        try:
            response = urllib.request.urlopen(req)
        except KeyboardInterrupt:
            print('interrupted!', file=sys.stderr)
            raise
        except:
            return S

        if S.get_digest:
            try:
                t = tempfile.NamedTemporaryFile()
                while 1:
                    buf = response.read(1024*512)
                    if not buf:
                        break
                    t.write(buf)
                t.flush()
                S.digest = mb.util.dgst(t.name)
                t.close()
            except:
                return S
        if S.get_content:
            S.content = response.read()

        if S.scheme == 'http':
            S.http_code = getattr(response, "code", None)
            if S.http_code == 200:
                S.has_file = True
            else:
                print('unhandled HTTP response code %r for URL %r' % (S.http_code, S.probeurl))
        elif S.scheme == 'ftp':
            # this works for directories. Not tested for files yet
            try:
                out = response.readline()
            except socket.timeout:
                # on an FTP URL with large directory, the listing may take longer than our socket TIMEOUT
                sys.stderr.write("\n%s timed out (%s)\n" %
                                 (S.identifier, S.probeurl))
                out = ''

            if len(out):
                S.has_file = True
            else:
                S.has_file = False

        return S

    elif S.scheme == 'rsync':

        try:
            tmpdir = tempfile.mkdtemp(prefix='mb_probefile_')
            # note the -r; *some* option is needed because many rsync servers
            # don't reply properly if they don't get any option at all.
            # -t (as the most harmless option) also isn't sufficient.
            #
            # replaced -r with -d, because it allows to probe for directories
            # without transferring them recursively. With 92 mirrors tested, it
            # worked just as well, with a single exception. (ftp3.gwdg.de, which
            # presumably runs a really old rsync server. The system seems to be
            # SuSE Linux 8.2.)
            # poeml, Mon Jun 22 18:10:33 CEST 2009
            cmd = ['rsync -d']
            if mb.util.get_rsync_version().startswith('3.'):
                cmd.append('--contimeout=%s' % TIMEOUT)
            cmd.append('--timeout=%d %s %s/' % (TIMEOUT, S.probeurl, tmpdir))

            if not S.get_content:
                cmd.append('--list-only')

            cmd = ' '.join(cmd)

            (rc, out) = subprocess.getstatusoutput(cmd)
            targetfile = os.path.join(tmpdir, os.path.basename(S.filename))
            if rc == 0 or os.path.exists(targetfile):
                S.has_file = True
            if S.has_file and S.get_digest:
                S.digest = mb.util.dgst(targetfile)
            if S.has_file and S.get_content:
                S.content = open(targetfile).read()

        finally:
            shutil.rmtree(tmpdir, ignore_errors=True)

        return S

    else:
        raise Exception('unknown URL type: %r' % S.probebaseurl)


def get_all_urls(mirror):
    r = []
    if mirror.baseurl:
        r.append(mirror.baseurl)
    if mirror.baseurlFtp:
        r.append(mirror.baseurlFtp)
    if mirror.baseurlRsync:
        r.append(mirror.baseurlRsync)
    return r


def get_best_scan_url(mirror):
    return mirror.baseurlRsync or mirror.baseurlFtp or mirror.baseurl or None


def make_probelist(mirrors, filename, url_type='http', get_digest=False, get_content=False):
    """return list of Sample instances, in order to be probed.
    The Sample instances are used to hold the probing results.
    """
    from mb.util import Sample
    if url_type == 'http':
        return [Sample(i.identifier, i.baseurl, filename,
                       get_digest=get_digest, get_content=get_content)
                for i in mirrors]
    elif url_type == 'scan':
        return [Sample(i.identifier, get_best_scan_url(i), filename,
                       get_digest=get_digest, get_content=get_content)
                for i in mirrors]
    elif url_type == 'all':
        return [Sample(i.identifier, url, filename,
                       get_digest=get_digest, get_content=get_content)
                for i in mirrors
                for url in get_all_urls(i)]
    else:
        raise Exception('unknown url_type value: %r' % url_type)


def probe_report(m):
    m = probe(m)
    # print ('checked %s' % m.probeurl)
    sys.stdout.write('.')
    sys.stdout.flush()
    return m

# TODO:
# for do_scan:
#  - get list of mirrors that have directory or file foo
#    find only the first URL
# for do_probefile:
#  - get list of mirrors that have directory or file foo, checking
#    all URLs
#  - optionally with md5 sums
# for the mirrorprobe?
# for general mirror testing?
# for timestamp fetching? -> get the content of the timestamp file

# use the multiprocessing module if available (Python 2.6/3.0)
# fall back to the processing module
# if none is availabe, serialize


def mirrors_have_file(mirrors, filename, url_type='all',
                      get_digest=False, get_content=False):
    mirrors = [i for i in mirrors]

    # we create a list of "simple" objects that can be serialized (pickled) by the
    # multiprocessing modules. That doesn't work with SQLObjects's result objects.
    return probes_run(make_probelist(mirrors, filename,
                                     url_type=url_type,
                                     get_digest=get_digest,
                                     get_content=get_content))


def lookups_probe(mirrors, get_digest=False, get_content=False):
    from mb.util import Sample
    probelist = [Sample(i['identifier'], i['baseurl'], i['path'],
                        get_digest=get_digest, get_content=get_content)
                 for i in mirrors]

    return probes_run(probelist)


def probes_run(probelist):
    mp_mod = None
    try:
        from multiprocessing import Pool
        mp_mod = 'multiprocessing'
    except:
        pass
    try:
        from processing import Pool
        mp_mod = 'processing'
    except:
        pass
    if len(probelist) > 8 and not mp_mod:
        print('>>> No multiprocessing module was found installed. For parallelizing')
        print('>>> probing, install the "processing" or "multiprocessing" Python module.')

    if mp_mod in ['processing', 'multiprocessing']:
        # FIXME make the pool size configurable
        # we don't need to spawn more processes then we have jobs to do
        if len(probelist) < 24:
            pool_size = len(probelist)
        else:
            pool_size = 24
        p = Pool(pool_size)
        result = p.map_async(probe_report, probelist)
        # print result.get(timeout=20)
        return result.get()

    else:
        res = []
        for i in probelist:
            res.append(probe_report(i))
        return res
