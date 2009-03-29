from sqlobject.sqlbuilder import AND


def has_file(conn, path, mirror_id):
    """check if file 'path' exists on mirror 'mirror_id'
    by looking at the database.
    
    path can contain wildcards, which will result in a LIKE match.
    """
    if path.find('*') >= 0 or path.find('%') >= 0:
        pattern = True
        oprtr = 'like'
        path = path.replace('*', '%')
    else:
        pattern = False
        oprtr = '='

    query = "SELECT mirr_hasfile_byname(%s, '%s')" \
                  % (mirror_id, path)
    result = conn.Server._connection.queryAll(query)[0][0]

    return result


def check_for_marker_files(conn, markers, mirror_id):
    """
    Check if all files in the markers list are present on a mirror,
    according to the database. 

    Markers actually a list of marker files.

    If a filename is prefixed with !, it negates the match, thus it can be used
    to check for non-existance.
    """
    found_all = True
    for m in markers.split():
        found_this = has_file(conn, m.lstrip('!'), mirror_id)
        if m.startswith('!'):
            found_this = not found_this
        found_all = found_all and found_this
    return found_all


def ls(conn, path):
    if path.find('*') >= 0 or path.find('%') >= 0:
        pattern = True
        oprtr = 'like'
        path = path.replace('*', '%')
    else:
        pattern = False
        oprtr = '='

    query = 'SELECT server.identifier, server.country, server.region, \
                       server.score, server.baseurl, server.enabled, \
                       server.status_baseurl, filearr.path \
                FROM filearr \
                LEFT JOIN server \
                ON server.id = ANY(filearr.mirrors) \
                WHERE filearr.path %s \'%s\' \
                ORDER BY server.region, server.country, server.score DESC' \
                  % (oprtr, path)
    rows = conn.Server._connection.queryAll(query)

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
        if pattern:
            d['path'] = i[7]
        else:
            d['path'] = path

        files.append(d)

    return files


def add(conn, path, mirror):

    query = """SELECT mirr_add_bypath(%d, '%s')""" \
               % (mirror.id, path)
    conn.Server._connection.queryAll(query)


def rm(conn, path, mirror):
    query = """SELECT mirr_del_byid(%d, (SELECT id FROM filearr WHERE path='%s'))""" \
                   % (mirror.id, path)
    conn.Server._connection.queryAll(query)


def dir_ls(conn, segments = 1, mirror=None):
    """Show distinct directory names, looking only on the first path components.

    Manually, this could be done in the following way:
    select distinct array_to_string((string_to_array(path, '/'))[0:2], '/') from filearr
    """

    query = """SELECT DISTINCT array_to_string(
                                   (string_to_array(path, '/'))[0:%s],
                                   '/') FROM filearr""" % segments

    if mirror:
        query += ' where %s = any(mirrors)' % mirror.id

    result = conn.Server._connection.queryAll(query)
    return result

def dir_show_mirrors(conn, path):
    """Show mirrors on which a certain directory path was found.
    """

    query = """select distinct(mirrors) from filearr where path like '%s%%'""" % path
    result = conn.Server._connection.queryAll(query)

    mirror_ids = []
    for i in result:
        i = i[0]
        for mirror_id in i:
            mirror_id = str(mirror_id)
            if mirror_id not in mirror_ids:
                mirror_ids.append(mirror_id)

    if not mirror_ids:
        return []
    query = """select identifier from server where id in (%s)""" % ','.join(mirror_ids)
    result = conn.Server._connection.queryAll(query)

    return result

