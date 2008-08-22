
import ConfigParser
import re


class Config:

    def __init__(self, conffile='/etc/mirrorbrain.conf', instance=None):

        self.config = {}

        cp = ConfigParser.SafeConfigParser()
        cp.read(conffile)

        self.config = dict(cp.items('general'))
        #print self.config

        # transform 'str1, str2, str3' form into a list
        re_clist = re.compile('[, ]+')
        self.config['instances'] = [ i.strip() for i in re_clist.split(self.config['instances'].strip()) ]
        self.instances = self.config['instances']

        self.instance = instance or self.instances[0]

        if not cp.has_section(self.instance):
            raise KeyError('The config does not have an instance \'%s\' defined' \
                      % self.instance)



        for i in self.instances:
            self.config[i] = dict(cp.items(i))


        self.dbconfig = self.config[self.instance]

        #print self.instance
        #print self.config
        #print self.dbconfig


    def get(self):
        print self.config
        return self.config

