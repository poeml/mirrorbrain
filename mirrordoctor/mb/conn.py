
import sys
import mberr
from sqlobject import *


server_show_template = """\
identifier     : %(identifier)s
operatorName   : %(operatorName)s
operatorUrl    : %(operatorUrl)s
baseurl        : %(baseurl)s
baseurlFtp     : %(baseurlFtp)s
baseurlRsync   : %(baseurlRsync)s
region         : %(region)s
country        : %(country)s
asn            : %(asn)s
prefix         : %(prefix)s
regionOnly     : %(regionOnly)s
countryOnly    : %(countryOnly)s
asOnly         : %(asOnly)s
prefixOnly     : %(prefixOnly)s
otherCountries : %(otherCountries)s
fileMaxsize    : %(fileMaxsize)s
publicNotes    : %(publicNotes)s
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
                          'asn',
                          'prefix',
                          'regionOnly',
                          'countryOnly',
                          'asOnly',
                          'prefixOnly',
                          'otherCountries',
                          'fileMaxsize',
                          'score',
                          'publicNotes',
                          'enabled',
                          'statusBaseurl',
                          'admin',
                          'adminEmail',
                          'operatorName',
                          'operatorUrl',
                          'comment' ]

def server2dict(s):
    return dict(identifier    = s.identifier,
                id            = s.id,
                baseurl       = s.baseurl,
                baseurlFtp    = s.baseurlFtp,
                baseurlRsync  = s.baseurlRsync,
                region        = s.region,
                country       = s.country,
                asn           = s.asn,
                prefix        = s.prefix,
                regionOnly    = s.regionOnly,
                countryOnly   = s.countryOnly,
                asOnly        = s.asOnly,
                prefixOnly    = s.prefixOnly,
                otherCountries = s.otherCountries,
                fileMaxsize   = s.fileMaxsize,
                score         = s.score,
                scanFpm       = s.scanFpm,
                publicNotes   = s.publicNotes,
                enabled       = s.enabled,
                statusBaseurl = s.statusBaseurl,
                comment       = s.comment,
                admin         = s.admin,
                adminEmail    = s.adminEmail,
                lat           = s.lat,
                lng           = s.lng,
                operatorName  = s.operatorName,
                operatorUrl   = s.operatorUrl)

#
# setup database connection
#

class Conn:
    def __init__(self, config, debug = False):
        dbdriver = config.get('dbdriver', 'mysql')
        if dbdriver in ['Pg', 'postgres', 'postgresql']:
            dbdriver, dbport = 'postgres', '5432'
            try: 
                import psycopg2
            except: 
                sys.exit('To use mb with PostgreSQL, you need the pcycopg2 Python module installed.')
            # see http://mirrorbrain.org/issues/issue27
            config['dbpass'] = config['dbpass'].replace(' ', r'\ ')
            config['dbpass'] = config['dbpass'].replace('\t', '\\\t')
            config['dbpass'] = config['dbpass'].replace("'", r"\'")
            if '"' in config['dbpass']:
                sys.exit('Sorry, but passwords cannot contain double quotes.')
        elif dbdriver in ['mysql']:
            dbport = '3306'
        else:
            sys.exit('database driver %r not known' % dbdriver)

        uri_str = dbdriver + '://%s:%s@%s:%s/%s'
        #if options.loglevel == 'DEBUG':
        #    uri_str += '?debug=1'
        self.uri = uri_str % (config['dbuser'], config['dbpass'], 
                              config['dbhost'], config.get('dbport', dbport), 
                              config['dbname'])

        sqlhub.processConnection = connectionForURI(self.uri)

        class Server(SQLObject):
            """the server table"""
            class sqlmeta:
                fromDatabase = True
        self.Server = Server

        class Filearr(SQLObject):
            """the file table"""
            class sqlmeta:
                fromDatabase = True
        self.Filearr = Filearr

        class Marker(SQLObject):
            """the marker table"""
            class sqlmeta:
                fromDatabase = True
        self.Marker = Marker

        class Country(SQLObject):
            """the countries table"""
            class sqlmeta:
                fromDatabase = True
                defaultOrder = 'code'
        self.Country = Country

        class Region(SQLObject):
            """the regions table"""
            class sqlmeta:
                fromDatabase = True
                defaultOrder = 'code'
        self.Region = Region

        try:
            class Pfx2asn(SQLObject):
                """the pfx2asn table"""
                class sqlmeta:
                    fromDatabase = True
                    defaultOrder = 'asn'
            self.Pfx2asn = Pfx2asn
        except psycopg2.ProgrammingError:
            # this is the error which we get if mod_asn doesn't happen
            # to be installed as well
            pass

        try:
            class Hash(SQLObject):
                """the hashes table"""
                class sqlmeta:
                    fromDatabase = True
                    idName = 'file_id'
            self.Hash = Hash
        except psycopg2.ProgrammingError:
            # This is what's raised if the table hasn't been installed yet
            # Which is the case when coming from a 2.12.0 or earlier install
            # XXX This feels like being totally the wrong place for a database migration.
            #     maybe a separate module with upgrade procedures to be run would be better.
            #     The main point is that this is a migration that we want to happen fully automatically.
            print >>sys.stderr
            print >>sys.stderr, '>>> A database table for hashes does not exit. Creating...'
            query = """
            CREATE TABLE "hash" (
                    "file_id" INTEGER REFERENCES filearr PRIMARY KEY,
                    "mtime" INTEGER NOT NULL,
                    "size" BIGINT NOT NULL,
                    "md5"    BYTEA NOT NULL,
                    "sha1"   BYTEA NOT NULL,
                    "sha256" BYTEA NOT NULL,
                    "sha1piecesize" INTEGER NOT NULL,
                    "sha1pieces" BYTEA,
                    "pgp" TEXT NOT NULL
            );
            """
            Filearr._connection.query(query)
            query = """
            CREATE VIEW hexhash AS
              SELECT file_id, mtime, size,
                     encode(md5, 'hex') AS md5,
                     encode(sha1, 'hex') AS sha1,
                     encode(sha256, 'hex') AS sha256,
                     sha1piecesize,
                     encode(sha1pieces, 'hex') AS sha1pieces,
                     pgp
              FROM hash;
            """
            Filearr._connection.query(query)
            print >>sys.stderr, '>>> Done.'
            print >>sys.stderr 
            # now try again
            class Hash(SQLObject):
                """the hashes table"""
                class sqlmeta:
                    fromDatabase = True
                    idName = 'file_id'
            self.Hash = Hash

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



