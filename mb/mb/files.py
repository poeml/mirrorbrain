from sqlobject.sqlbuilder import AND

from mb import util


def has_file(conn, path, mirror_id):
    """check if file 'path' exists on mirror 'mirror_id'
    by looking at the database.

    path can contain wildcards, which will result in a LIKE match.
    """
    if path.find('*') >= 0 or path.find('%') >= 0:
        oprtr = 'like'
        path = path.replace('*', '%')
    else:
        oprtr = '='

    query = "SELECT path FROM files JOIN mirrors ON id = file_id WHERE path %s '%s' AND %s = server_id" \
        % (oprtr, path, mirror_id)
    result = conn.Server._connection.queryAll(query)

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
    """If path contains a wildcard (* or %):

    Return all paths known to the database that start match the given path
    argument (containing wildcards).

    If path doesn't contain wildcards:

    Return the exact match on the path argument."""

    if path.find('*') >= 0 or path.find('%') >= 0:
        pattern = True
        oprtr = 'like'
        path = path.replace('*', '%')
    else:
        pattern = False
        oprtr = '='

    query = 'SELECT server.identifier, server.country, server.region, \
                       server.score, server.baseurl, server.enabled, \
                       server.status_baseurl, files.path \
                FROM files \
                LEFT JOIN server_files on file_id = files.id \
                LEFT JOIN server on server.id = server_id \
                WHERE path %s \'%s\' \
                ORDER BY server.region, server.country, server.score DESC' \
                  % (oprtr, path)
    rows = conn.Server._connection.queryAll(query)

    files = []
    # ugly. Really need to let an ORM do this.
    for i in rows:
        d = {'identifier':     i[0],
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

    query = """SELECT mb_mirror_add_file(%d, '%s')""" \
        % (mirror.id, path)
    conn.Server._connection.queryAll(query)


def rm(conn, path, mirror):
    query = """DELETE FROM server_files WHERE server_id = %d AND file_id in (SELECT id FROM files WHERE path='%s'))""" \
        % (mirror.id, path)
    conn.Server._connection.queryAll(query)


def dir_ls(conn, segments=1, mirror=None):
    """Show distinct directory names, looking only on the first path components.
    """

    query = """SELECT DISTINCT array_to_string(
                                   (string_to_array(path, '/'))[0:%s],
                                   '/') FROM files""" % segments

    if mirror:
        query += ' JOIN server_files on id = file_id WHERE %s = server_id' % mirror.id

    result = conn.Server._connection.queryAll(query)
    return result


def dir_show_mirrors(conn, path, missing=False):
    """Show mirrors on which a certain directory path was found.
    The path could actually also be a file, it doesn't matter, but
    directory is what we are looking for in the context that this function was
    written for.
    """

    query = """select distinct(array_agg(server_id)) from files join server_files on id = file_id where path like '%s%%' group by file_id""" % path
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
    if not missing:
        query = """select identifier from server where enabled and id in (%s)""" % ','.join(
            mirror_ids)
    else:
        query = """select identifier from server where enabled and id not in (%s)""" % ','.join(
            mirror_ids)
    result = conn.Server._connection.queryAll(query)

    return result


def dir_filelist(conn, path):
    """Returns tuples of (id, name) for all files that reside in a directory

    The returned filenames include their path."""

    query = """SELECT path, id
                   FROM files
               WHERE path ~ '^""" + util.pgsql_regexp_esc(path) +"""/[^/]*$'"""

    result = conn.Server._connection.queryAll(query)
    return result

