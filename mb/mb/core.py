import mb.mberr


class Directory:
    def __init__(self, name):
        self.name = name
        self.files = []

    def __str__(self):
        # return '%s:\n%s' % (self.name, '\n'.join(self.files))
        return '%-45s: %6s files' % (self.name, int(len(self.files)))


def delete_mirror(conn, mirror):
    """delete a mirror by specifying its (exact) identifier string)"""
    try:
        m = conn.Server.select(conn.Server.q.identifier == mirror)[0]
    except IndexError:
        raise mb.mberr.MirrorNotFoundError(mirror)

    # Delete relates prefix and asn values for the server
    conn.Serverpfx._connection.query(
        "DELETE from serverpfx where serverid=%d" % m.id)

    conn.Server.delete(m.id)


def mirror_get_nfiles(conn, mirror):
    query = """SELECT mirr_get_nfiles(%d)""" % (mirror.id)
    return conn.Server._connection.queryAll(query)[0]
