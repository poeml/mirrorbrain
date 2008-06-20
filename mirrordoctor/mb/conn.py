
from sqlobject import *
from mb.conf import config

#
# setup database connection
#
uri_str = 'mysql://%s:%s@%s:%s/%s'
#if options.loglevel == 'DEBUG':
#    uri_str += '?debug=1'
uri = uri_str % (config['dbuser'], config['dbpass'], config['dbhost'], config['dbport'], config['dbname'])

sqlhub.processConnection = connectionForURI(uri)


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
                score         = s.score,
                enabled       = s.enabled,
                statusBaseurl = s.statusBaseurl,
                comment       = s.comment,
                admin         = s.admin,
                adminEmail    = s.adminEmail)

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
    

def servers_match(match):
    servers = Server.select("""identifier LIKE '%%%s%%'""" % match)
    return list(servers)


class Server(SQLObject):
    """the server table"""
    class sqlmeta:
        fromDatabase = True

#class Asn(SQLObject):
#    """the autonomous systems table"""
#    class sqlmeta:
#        fromDatabase = True
#
