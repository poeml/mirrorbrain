
from sqlobject import *


server_show_template = """\
identifier     : %(identifier)s
id             : %(id)s
baseurl        : %(baseurl)s
baseurlFtp     : %(baseurlFtp)s
baseurlRsync   : %(baseurlRsync)s
region         : %(region)s
country        : %(country)s
countryOnly    : %(countryOnly)s
regionOnly     : %(regionOnly)s
otherCountries : %(otherCountries)s
fileMaxsize    : %(fileMaxsize)s
score          : %(score)s
enabled        : %(enabled)s
statusBaseurl  : %(statusBaseurl)s
admin          : %(admin)s
adminEmail     : %(adminEmail)s
---------- comments ----------
%(comment)s
---------- comments ----------
"""


server_editable_attrs = [ 'baseurl',
                          'baseurlFtp',
                          'baseurlRsync',
                          'region',
                          'country',
                          'countryOnly',
                          'regionOnly',
                          'otherCountries',
                          'fileMaxsize',
                          'score',
                          'enabled',
                          'statusBaseurl',
                          'admin',
                          'adminEmail',
                          'comment' ]

def server2dict(s):
    return dict(identifier    = s.identifier,
                id            = s.id,
                baseurl       = s.baseurl,
                baseurlFtp    = s.baseurlFtp,
                baseurlRsync  = s.baseurlRsync,
                region        = s.region,
                country       = s.country,
                countryOnly   = s.countryOnly,
                regionOnly    = s.regionOnly,
                otherCountries = s.otherCountries,
                fileMaxsize   = s.fileMaxsize,
                score         = s.score,
                enabled       = s.enabled,
                statusBaseurl = s.statusBaseurl,
                comment       = s.comment,
                admin         = s.admin,
                adminEmail    = s.adminEmail)

#
# setup database connection
#

class Conn:
    def __init__(self, config, debug = False):
        uri_str = 'mysql://%s:%s@%s:%s/%s'
        #if options.loglevel == 'DEBUG':
        #    uri_str += '?debug=1'
        self.uri = uri_str % (config['dbuser'], config['dbpass'], 
                              config['dbhost'], config.get('dbport', '3306'), 
                              config['dbname'])

        sqlhub.processConnection = connectionForURI(self.uri)

        class Server(SQLObject):
            """the server table"""
            class sqlmeta:
                fromDatabase = True
        self.Server = Server

        class File(SQLObject):
            """the file table"""
            class sqlmeta:
                fromDatabase = True
        self.File = File

        class FileServer(SQLObject):
            """the file_server table"""
            class sqlmeta:
                fromDatabase = True
        self.FileServer = FileServer

        class Marker(SQLObject):
            """the marker table"""
            class sqlmeta:
                fromDatabase = True
                defaultOrder = 'subtree_name'
        self.Marker = Marker

        if debug:
            self.Server._connection.debug = True



def servertext2dict(s):
    import re
    import mb.conn

    new_attrs = dict()
    for a in mb.conn.server_editable_attrs:
        m = re.search(r'^%s *: (.*)' % a, 
            s, re.MULTILINE)
        if m:
            if m.group(1) != 'None':
                new_attrs[a] = m.group(1).rstrip()
            else:
                new_attrs[a] = None

    # the comment field is formatted differently
    comment_delim = '---------- comments ----------'
    new_attrs['comment'] = s.split(comment_delim)[1].strip('\n')

    return new_attrs
    

def servers_match(server, match):
    servers = server.select("""identifier LIKE '%%%s%%'""" % match)
    return list(servers)



