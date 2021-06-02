
import os
import mb.core

# split lines, but take into account that filenames could contain spaces
#
# >>> a = '-rw-r--r--      4405843968 2007/09/27 17:50:25 distribution/10.3/iso/dvd/openSUSE-10.3-GM- DVD-i386.iso'
# >>> a.split(None, 4)
# ['-rw-r--r--', '4405843968', '2007/09/27', '17:50:25', 'distribution/10.3/iso/dvd/openSUSE-10.3-GM- DVD-i386.iso']


def get_filelist(url):
    import urllib
    import subprocess

    print url
    url = list(urllib.urlparse(url))
    if not ':' in url[1]:
        url[1] += ':873'
    url = urllib.urlunparse(url)
    print url
    # old
    ###  child_stdin, child_stdout, child_stderr = os.popen3(['rsync', '-r', url])
    # child_stdin, child_stdout, child_stderr = os.popen3(['cat', 'buildservice-repos.txt'])
    # child_stdin.close()

    # new
    ###  p = subprocess.Popen(cmd, shell=True, bufsize=bufsize, stdin=None, stdout=PIPE, stderr=PIPE, close_fds=True)
    ###  (child_stdin, child_stdout, child_stderr) = (p.stdin, p.stdout, p.stderr)

    # newer
    o = subprocess.Popen(['rsync', '-r', url], stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, close_fds=True).stdout
    print 'done'
    #import time
    # time.sleep(100)

    dirCollection = {}
    # for line in child_stdout:
    for line in o.readlines():
        print 'a line'
        try:
            mode, size, date, time, name = line.split(None, 4)
        except:
            # may be the stupid motd
            import sys
            print 'could not parse this line: %s' % repr(line)
            #sys.exit('could not parse this line:\n%s' % repr(line))
            continue
        name = name.rstrip()

        if mode.startswith('d'):
            dirCollection[name] = mb.core.Directory(name)

        elif mode.startswith('-'):
            d, p = os.path.split(name)
            if not d:
                d = '.'
            dirCollection[d].files.append(p)

        elif mode.startswith('l'):
            # we ignore symbolic links
            continue
        else:
            # something unknown...
            print 'skipping', line

    err = child_stderr.read()

    return dirCollection, err
