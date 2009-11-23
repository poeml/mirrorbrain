#!/usr/bin/python

import sys
import os

(basedir, name, size, mtime) = sys.argv[1:5]

print 'Creating %s/%s (%s bytes, mtime %s)' % (basedir, name, size, mtime)
size = int(size)
mtime = int(mtime)
atime = mtime

path = os.path.join(basedir, name)
canonical_path = os.path.realpath(path)

# for safety
if not canonical_path.startswith(basedir):
    sys.exit("canonical path (%r) doesn't start with the basedir (%r)")


try:
    os.makedirs(os.path.dirname(canonical_path))
except:
    pass

# FIXME: it is inefficient to delete and recreate the files all the time
# but for now it allows this prototype to get forward
# the removal (or a check in general) is needed because the file size
# could shrink

try:
    os.unlink(canonical_path)
except:
    pass

fd = open(canonical_path, 'w')

if size == 0:
    fd.truncate()
else:
    fd.seek(size - 1)
    fd.write('\0')
fd.close()

os.utime(canonical_path, (atime, mtime))

