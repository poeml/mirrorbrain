#!/usr/bin/python

# Copyright 2008,2009 Peter Poeml
#
#     This program is free software; you can redistribute it and/or
#     modify it under the terms of the GNU General Public License version 2
#     as published by the Free Software Foundation;
#
#     This program is distributed in the hope that it will be useful,
#     but WITHOUT ANY WARRANTY; without even the implied warranty of
#     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#     GNU General Public License for more details.
#
#     You should have received a copy of the GNU General Public License
#     along with this program; if not, write to the Free Software
#     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
#
#
#
# Analyze Apache logfiles in order to count downloads
#
#
# This script parses a MirrorBrain-enhanced access_log and does the following:
#   - a little ring buffer filters requests recurring within a sliding time window (keyed by ip+url+referer+user-agent)
#   - strip trailing http://... cruft
#   - remove duplicated slashes
#   - remove accidental query strings
#   - remove a possible .metalink suffix
#   - remove the /files/ prefix
# 
# It applies filtering by
#   - status code being 200 or 302
#   - requests must be GET
#   - bouncer's IP which keeps coming back to download all files (from OOo)
# 
# It also captures the country where the client requests originate from.
#
# This script uses Python generators, which means that it doesn't allocate
# memory according to the log size. It rather works like a Unix pipe.
# (The implementation of the generator pipeline is based on David Beazley's
# PyCon UK 08 great talk about generator tricks for systems programmers.)
#
# 
# I baked a first regexp which is able to parse most (OpenOffice.org) requests
# from /stable and /extended. There are some exceptions (language code with 3
# letters) and I didn't take care of /localized yet.
# 
# The script should serve as model implementation for the Apache module which
# does the same in realtime.
#
#
# Usage: 
# ./dlcount.py /var/log/apache2/download.services.openoffice.org/2009/11/download.services.openoffice.org-20091123-access_log.bz2 | sort -u
#
# Uncompressed, gzip or bzip2 compressed files are transparently opened.
# 
#
# 


__version__='0.9'
__author__='Peter Poeml <poeml@cmdline.net>'
__copyright__='Peter poeml <poeml@cmdline.net>'
__license__='GPLv2'
__url__='http://mirrorbrain.org/'


import re

try:
    set
except NameError:
    from sets import Set as set     # Python 2.3 fallback

try:
    sorted
except NameError:
    def sorted(in_value):           # Python 2.3 fallback
        "A naive implementation of sorted"
        out_value = list(in_value)
        out_value.sort()
        return out_value


def gen_open(filenames): 
    """Open a sequence of filenames"""
    import gzip, bz2 
    for name in filenames: 
        if name.endswith(".gz"): 
             yield gzip.open(name) 
        elif name.endswith(".bz2"): 
             yield bz2.BZ2File(name) 
        else: 
             yield open(name) 

def gen_cat(sources): 
    """Concatenate items from one or more 
    source into a single sequence of items"""
    for s in sources: 
        for item in s: 
            yield item 


def gen_grep(pat, lines): 
    import re 
    patc = re.compile(pat) 
    for line in lines: 
        if patc.search(line): yield line 

def gen_fragments(pat, lines): 
    """Generate a sequence of line fragments, according to
    a given regular expression"""
    import re 
    patc = re.compile(pat) 
    for line in lines: 
        m = patc.match(line)
        if m:
            yield m.groups()


class RingBuffer:
    """Here is a simple circular buffer, or ring buffer, implementation in
    Python. It is a first-in, first-out (FIFO) buffer with a fixed size.

    Here is an example where the buffer size is 4. Ten integers, 0-9, are
    inserted, one at a time, at the end of the buffer. Each iteration, the first
    element is removed from the front of the buffer.
    
    buf = RingBuffer(4)
    for i in xrange(10):
        buf.append(i)
        print buf.get()
    
    
    Here are the results:
    
    [None, None, None, 0]
    [None, None, 0, 1]
    [None, 0, 1, 2]
    [0, 1, 2, 3]
    [1, 2, 3, 4]
    [2, 3, 4, 5]
    [3, 4, 5, 6]
    [4, 5, 6, 7]
    [5, 6, 7, 8]
    [6, 7, 8, 9]
    
    from http://www.saltycrane.com/blog/2007/11/python-circular-buffer/
    """
    def __init__(self, size):
        self.data = [None for i in xrange(size)]

    def append(self, x):
        self.data.pop(0)
        self.data.append(x)

    def get(self):
        return self.data



def main():
    """
    Create a generator pipeline for the matching log file lines
    and process them.
    """
    import re
    import sys
    import hashlib

    if not len(sys.argv[1:]):
        sys.exit('Usage: dlcount LOGFILE [LOGFILE ...]')



    # best reference about Python regexp: http://www.amk.ca/python/howto/regex/regex.html
    #
    # short intro to things that *may* be special: 
    #   (?:   )         non-capturing group
    #   (?P<foo>    )   named group
    # (FIXME: need to check if all these are supported in Apache)
    #
    matchlist = [ 
        # stable/3.1.1/OOo_3.1.1_Win32Intel_install_en-US.exe
        # stable/3.1.1/OOo_3.1.1_MacOSXIntel_install_en-US.dmg
        # stable/3.1.1/OOo_3.1.1_Win32Intel_install_wJRE_en-US.exe
        # extended/3.1.1rc2/OOo_3.1.1rc2_20090820_Win32Intel_langpack_en-ZA.exe      -
        # extended/3.1.1rc2/OOo_3.1.1rc2_20090820_Win32Intel_langpack_en-ZA.exe      -
        # extended/3.1.1rc2/OOo_3.1.1rc2_20090820_Win32Intel_langpack_en-ZA.exe      -
        # extended/3.1.1rc2/OOo_3.1.1rc2_20090820_LinuxIntel_langpack_brx_deb.tar.gz
        # extended/developer/DEV300_m65/OOo-Dev-SDK_DEV300_m65_Win32Intel_install_en-US.exe
        ( r'^(?:stable|extended)/(?:developer/)?([^/]+)/(OOo|OOo-SDK|OOo-Dev|OOo-Dev-SDK)_(?P<realversion>[^_]+(?:_[0-9]+)?)_(.+)_(?P<lang>([a-zA-Z]{2}(-[a-zA-Z]{2})?|binfilter|core|l10n|extensions|system|testautomation|brx|dgo|kok|mai|mni|sat))(_deb|_rpm)?\.(exe|dmg|sh|tar\.gz|tar\.bz2)$', r'prod: \2  os: \4  version: \1  realversion: \g<realversion>  lang: \g<lang>'),


        # extended/3.1.1rc2/OOo_3.1.1rc2_20090820_LinuxX86-64_langpack_zh-CN.tar.gz
        # extended/3.1.1rc2/OOo_3.1.1rc2_20090820_LinuxX86-64_langpack_zh-CN_deb.tar.gz

        # localized/ru/2.4.3/OOo_2.4.3_Win32Intel_install_ru.exe      -
        # localized/es/2.4.3/OOo_2.4.3_Win32Intel_install_es.exe      -

    ]
    re_matchlist = []
    for match, sub in matchlist:
        re_matchlist.append((re.compile(match), sub, match))



    DUP_WINDOW = 200
    known = RingBuffer(DUP_WINDOW)

    filenames = sys.argv[1:]
    logfiles = gen_open(filenames)
    loglines = gen_cat(logfiles)

    # 123.123.123.123 - - [23/Nov/2009:18:19:14 +0100] "GET /files/stable/3.1.1/OOo_3.1.1_MacOSXIntel_install_en-US.dmg HTTP/1.1" 302 399 "http://download.openoffice.org/all_rc.html" "Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 1.1.4322; .NET CLR 3.5.30729; .NET CLR 3.0.30618)" ftp.astral.ro r:country 913 844 EU:RO ASN:9050 P:92.81.0.0/16 size:24661382 -
    # 200 is returned for files that are not on mirrors, and for metalinks
    pat = r'^(\S+).+"GET (\S*) HTTP.*" (200|302) [^"]+ "([^"]*)" "([^"]*)".* \w\w:(\w\w) ASN:'
    reqs = gen_fragments(pat, loglines)

    re_strip_protocol = re.compile(r'^http://[^/]+/')
    re_single_slashes = re.compile(r'/+')
    re_strip_queries = re.compile(r'\?.*')
    re_strip_prefix = re.compile(r'^/files/')
    re_strip_metalink = re.compile(r'\.metalink$')


    for req in reqs:

        (ip, url, status, referer, ua, country) = req

        # over a window of DUP_WINDOW last requests, the same request must
        # not have occured already
        m = hashlib.md5()
        m.update(repr(req))
        md = m.digest()

        # FIXME
        if ip == '140.211.167.212':
            # that's osuosl.org's Bouncer host
            continue

        # was the requests seen recently? If yes, ignore it.
        # otherwise, put it into the ring buffer.
        if md in known.data:
            continue
        known.append(md)


        # note that we could use .replace() for many of these, but for compatibility with
        # an Apache module in C we'll follow a pure regex-based approach
        url = re_strip_protocol.sub('', url)
        url = re_single_slashes.sub('/', url)
        # FIXME: should we rather ignore requests with query string?
        url = re_strip_queries.sub('', url)
        url = re_strip_prefix.sub('', url)
        url = re_strip_metalink.sub('', url)

        print '%-80s ' % url, 

        matched = False
        for m, s, mreg in re_matchlist:
            if matched:
                sys.exit('warning: %r matches\n   %r\nbut already matched a pevious regexp:\n   %r' % (url, mreg, matched))
            if m.match(url):
                print m.sub(s, url)
                matched = mreg
        if not matched:
            print '-'


    sys.exit(0)


if __name__ == '__main__':
    main()

