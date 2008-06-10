
import os
import mb.core

# split lines, but take into account that filenames could contain spaces
#
# >>> a = '-rw-r--r--      4405843968 2007/09/27 17:50:25 distribution/10.3/iso/dvd/openSUSE-10.3-GM- DVD-i386.iso'
# >>> a.split(None, 4)
# ['-rw-r--r--', '4405843968', '2007/09/27', '17:50:25', 'distribution/10.3/iso/dvd/openSUSE-10.3-GM- DVD-i386.iso']

def get_filelist(url):
    child_stdin, child_stdout, child_stderr = os.popen3(['rsync', '-r', url])
    #child_stdin, child_stdout, child_stderr = os.popen3(['cat', 'buildservice-repos.txt'])
    child_stdin.close()

    dirCollection = {}
    for line in child_stdout:
        try:
            mode, size, date, time, name = line.split(None, 4)
        except:
            print repr(line)
            import sys
            sys.exit(1)
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


