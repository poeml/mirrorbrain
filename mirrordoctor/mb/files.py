from mb.util import b64_md5

def ls(conn, path, pattern = False):

    if pattern:
        query = 'SELECT server.identifier, server.country, server.region, \
                           server.score, server.baseurl, server.enabled, \
                           server.status_baseurl \
                    FROM file \
                    LEFT JOIN file_server \
                    ON file.id = file_server.fileid \
                    LEFT JOIN server \
                    ON file_server.serverid = server.id \
                    WHERE file.path like \'%s\' \
                    ORDER BY server.region, server.country, server.score DESC' \
                      % path
    else:
        query = 'SELECT server.identifier, server.country, server.region, \
                           server.score, server.baseurl, server.enabled, \
                           server.status_baseurl \
                    FROM file_server \
                    LEFT JOIN server \
                    ON file_server.serverid = server.id \
                    WHERE file_server.path_md5=\'%s\' \
                    ORDER BY server.region, server.country, server.score DESC' \
                      % b64_md5(path)

    rows = conn.FileServer._connection.queryAll(query)

    files = []
    # ugly. Really need to let an ORM do this.
    for i in rows:
        d = { 'identifier':     i[0],
              'country':        i[1] or '',
              'region':         i[2] or '',
              'score':          i[3] or 0,
              'baseurl':        i[4] or '<base url n/a>',
              'enabled':        i[5],
              'status_baseurl': i[6], }

        files.append(d)

    return files


def add(conn, path, mirror):

    f = conn.File(path = path)
    #print 'file id:', f.id

    # this doesn't work because the table doesn't have a primary key 'id'...
    # (our primary Key consists only of number columns)
    #import datetime
    #fs = conn.FileServer(fileid = f.id,
    #                     serverid = mirror.id,
    #                     pathMd5 = b64_md5(path),
    #                     timestampScanner = datetime.datetime.now())
    #print fs

    query = """INSERT INTO file_server SET fileid=%d, serverid=%d, path_md5='%s'""" \
               % (f.id, mirror.id, b64_md5(path))
    conn.FileServer._connection.queryAll(query)


def rm(conn, path, mirror):
    query = """DELETE FROM file_server WHERE serverid=%s AND path_md5='%s'""" \
                 % (mirror.id, b64_md5(path))
    conn.FileServer._connection.queryAll(query)

