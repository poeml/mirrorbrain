import re
from mechanize import Browser
import mb.core 

_match_dir = re.compile(r"""
            ^(?!(http|mailto|\?|/))     # doesn't start with http, mailto, 
                                        # or slash (because that's the link to the 
                                        # parent dir)
            .*
            /$                          # ends in a slash
            """, re.X)

_match_file = re.compile(r"""
            ^(?!(http|mailto|\?|/))     # same as above
            .*
            [^/]$                       # ends not in a slash
            """, re.X)


def get_filelist(start_url):

    if not start_url.endswith('/'):
        start_url += '/'
    start_url_len = len(start_url)

    dirCollection = {}

    directories_todo = [start_url]

    # walk directories non-recursively
    # http://aspn.activestate.com/ASPN/Cookbook/Python/Recipe/435875
    while len(directories_todo) > 0:
        print '+', len(dirCollection.keys())
        print '-', len(directories_todo)
        #if len(directories_todo) == 175:
        #    return dirCollection, 'foo'


        directory = directories_todo.pop()
        #if '/drpmsync/' in directory or '/repositories' in directory:
        #    print '>>>>>> skipping', directory
        #    continue
        print '>'
        print directory

        name = directory[start_url_len:].rstrip('/') or '.'
        dirCollection[name] = mb.core.Directory(name)

        br = Browser()
        br.open(directory)

        # found files
        for i in br.links(url_regex = _match_file):
            #if i.url.startswith('./'):
            #    i.url = i.url[2:]
            #print 'appending file', i.url
            dirCollection[name].files.append(i.url)

        found_dirs  = [ link.base_url + link.url.lstrip('./') for link in br.links(url_regex = _match_dir) ]
        found_dirs = []
        for link in br.links(url_regex = _match_dir):
            if link.url.startswith('./'):
                link.url = link.url[2:]
            found_dirs.append(link.base_url + link.url)

        print 'found_dirs:', found_dirs
        #found_files = [ link.base_url + link.url for link in br.links(url_regex = _match_file) ]
        #print 'found_files:', found_files
        br.close()
        
        # found directories
        for found_dir in found_dirs:
            br = Browser()
            br.open(found_dir)

            name = found_dir[start_url_len:].rstrip('/')
            print 'name:', name
            dirCollection[name] = mb.core.Directory(name)

            for i in br.links(url_regex = _match_file):
                dirCollection[name].files.append(i.url)

            for i in br.links(url_regex = _match_dir):
                if i.url.startswith('./'):
                    i.url = i.url[2:]
                print 'neues todo:', i.base_url + i.url
                directories_todo.append(i.base_url + i.url)

            br.close()


    del br

    return dirCollection, 'foo'

    #print 'directories:'
    #for link in br.links(url_regex = _match_dir):
    #    #print link
    #    print link.base_url[start_url_len:] + link.url

    #print
    #print 'files:'
    #for link in br.links(url_regex = _match_file):
    #    #print link
    #    print link.base_url[start_url_len:] + link.url

