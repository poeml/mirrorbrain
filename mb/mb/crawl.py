

from mechanize import Browser
import sys
import os
import re


def get_filelist(url):
    child_stdin, child_stdout, child_stderr = os.popen3(['rsync', '-r', url])
    #child_stdin, child_stdout, child_stderr = os.popen3(['cat', 'buildservice-repos.txt'])
    child_stdin.close()

    dirs = {}
    for line in child_stdout:
        # split line, but take into account that filenames could contain spaces
        #
        # >>> a = '-rw-r--r--      4405843968 2007/09/27 17:50:25 distribution/10.3/iso/dvd/openSUSE-10.3-GM- DVD-i386.iso'
        # >>> a.split(None, 4)
        # ['-rw-r--r--', '4405843968', '2007/09/27', '17:50:25', 'distribution/10.3/iso/dvd/openSUSE-10.3-GM- DVD-i386.iso']
        try:
            mode, size, date, time, name = line.split(None, 4)
        except:
            print(repr(line))
            import sys
            sys.exit(1)
        name = name.rstrip()

        if mode.startswith('d'):
            dirs[name] = Directory(name)

        elif mode.startswith('-'):
            d, p = os.path.split(name)
            if not d:
                d = '.'
            dirs[d].files.append(p)

        elif mode.startswith('l'):
            # we ignore symbolic links
            continue
        else:
            # something unknown...
            print('skipping', line)

    err = child_stderr.read()

    return dirs, err


burl, url = sys.argv[1], sys.argv[2]
#burl_len = len('http://widehat.opensuse.org/')
#burl_len = len('http://opensuse.unixheads.net/')
#burl_len = len('http://download.opensuse.org/pub/opensuse/')
burl_len = len(burl)

br = Browser()
br.open(url)


print('directories:')
for link in br.links(url_regex=re.compile(r"""
        ^(?!(http|mailto|\?|/)) 
        .*
        /$
        """, re.X)):
    # print (link.url)
    print(link.base_url[burl_len:] + link.url)

print()
print('files:')
for link in br.links(url_regex=re.compile(r"""
        ^(?!(http|mailto|\?|/))
        .*
        [^/]$
        """, re.X)):
    # print (link)
    print(link.base_url[burl_len:] + link.url)

for line in get_filelist('rsync.opensuse.org::opensuse-updates'):
    print(line)
