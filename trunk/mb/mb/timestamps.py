# encoding: utf-8

import os
import sys
import time
import tempfile
import pwd, grp

explanation = """
Should you wonder about this file, it supplies timestamps that can be
used to assess possible lags in mirroring.

These are not used by MirrorBrain for mirror checking, but can help
human beings with verifying a mirrors setup. (Maybe MirrorBrain could/should
make use of these timestamps in the future.)

In addition, the .timestamp_invisible is supposed to be not visible
through HTTP, FTP or rsync. This serves to ensure that a mirrors's
permission setup is correct. Keeping certain files temporarily
unreadable can be an important step in the process of publishing content.

Feel free to contact mirrorbrain at mirrorbrain org for more information;
Thanks.

"""

def create(tstamps, user=None, group=None):

    epoch = int(time.time())
    utc = time.strftime("%a, %d %b %Y %H:%M:%S UTC", time.gmtime())


    if user:
        user = pwd.getpwnam(user).pw_uid 
    else:
        user = os.geteuid()

    if group:
        group = grp.getgrnam(group).gr_gid
    else:
        group = os.getegid()

    for tstamp in tstamps:
        try:
            # we might write in a directory not owned by root
            (fd, tmpfilename) = tempfile.mkstemp(prefix = os.path.basename(tstamp), 
                                                 dir = os.path.dirname(tstamp))
        except OSError, e:
            sys.exit(e)

        if tstamp.endswith('invisible'):
            mode = 0640
        else:
            mode = 0644

        try:
            os.chown(tmpfilename, 
                     user, 
                     group)
        except OSError, e:
            sys.exit(e)

        os.chmod(tmpfilename, mode)
        
        f = os.fdopen(fd, 'w')
        f.write('%s\n%s\n\n' % (epoch, utc))
        f.write(explanation)
        f.close()

        os.rename(tmpfilename, tstamp)

