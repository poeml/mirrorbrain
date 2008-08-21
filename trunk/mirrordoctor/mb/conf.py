
import ConfigParser
import re


class Config:

    def __init__(self, conffile):

        self.config = {}

        cp = ConfigParser.SafeConfigParser()
        cp.read(conffile)

        self.config = dict(cp.items('general'))

        # transform 'str1, str2, str3' form into a list
        re_clist = re.compile('[, ]+')
        self.config['instances'] = [ i.strip() for i in re_clist.split(self.config['instances'].strip()) ]

        instances = self.config['instances']
        #self.config['default_instance'] = instances[0]

        for i in instances:
            self.config[i] = dict(cp.items(i))
        return 

    def get(self):
        return self.config

