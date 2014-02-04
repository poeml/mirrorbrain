
import sys
import os
import ConfigParser
import re
import mb.mberr


boolean_opts = [ 'zsync_hashes', 'chunked_hashes' ]

DEFAULTS = { 'zsync_hashes': False,
             'chunked_hashes': True,
             'chunk_size': 262144 }

class Config:
    """this class sets up a number dictionaries that contain configuration 
    read from a configuration file (per default form mirrorbrain.conf):

    self.general       # the [general] section
                         it also contains the auth data from all [instance] sections
                         to be accessed by the respective instance name
    self.dbconfig      # the database configuration for the chosen instance
    self.mirrorprobe   # the [mirrorprobe] section

    """

    def __init__(self, conffile='/etc/mirrorbrain.conf', instance=None):

        self.general = {}
        self.dbconfig = {}
        self.mirrorprobe = {}

        if not os.path.exists(conffile):
            raise mb.mberr.NoConfigfile(conffile, 'No config file found. Please refer to:\n'
                    'http://mirrorbrain.org/docs/installation/initial_config/#create-mirrorbrain-conf')

        cp = ConfigParser.SafeConfigParser()
        try:
            cp.read(conffile)
        except ConfigParser.ParsingError, e:
            print e
            sys.exit(2)

        #
        # take care of the [general] section
        #
        self.general = dict(cp.items('general'))

        # transform 'str1, str2, str3' form into a list
        re_clist = re.compile('[, ]+')
        self.general['instances'] = [ i.strip() for i in re_clist.split(self.general['instances'].strip()) ]
        self.instances = self.general['instances']

        self.instance = instance or self.instances[0]

        if not self.instance in self.instances:
            raise KeyError('Config error: \'%s\' is not listed in instances' \
                      % self.instance)
            
        #
        # collect the database auth sections
        #
        for i in self.instances:
            if not cp.has_section(i):
                raise KeyError('The config does not have a section named [%s] '
                               'for the instance %r' % (i, i))
            self.general[i] = dict(cp.items(i))
            for b in boolean_opts:
                try:
                    self.general[i][b] = cp.getboolean(i, b)
                except ValueError, e:
                    raise mb.mberr.ConfigError('cannot parse setting in [%s] section: %r' % (i, b + str(e)), conffile)
                except ConfigParser.NoOptionError, e:
                    pass
            # set default values where the config didn't define anything
            for d in DEFAULTS:
                try: 
                    self.general[i][d]
                except:
                    self.general[i][d] = DEFAULTS[d]

            self.general[i]['chunk_size'] = int(self.general[i]['chunk_size'])
            if self.general[i]['zsync_hashes']:
                # must be a multiple of 2048 and 4096 for zsync checksumming
                assert self.general[i]['chunk_size'] % 4096 == 0




        # all database configs are accessible via self.general, but 
        # let's put the one of the chosen instance into
        # self.dbconfig
        self.dbconfig = self.general[self.instance]

        #
        # take care of the [mirrorprobe] section
        #
        self.mirrorprobe = dict(cp.items('mirrorprobe'))


