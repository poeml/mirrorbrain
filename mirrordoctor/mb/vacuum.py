
def stale(conn):
    """show statistics about stale files in the database"""

    # file_server table
    query = 'SELECT COUNT(*) FROM file_server fs LEFT OUTER JOIN server s ON fs.serverid = s.id WHERE s.id IS NULL'
    n_file_stale = conn.FileServer._connection.queryAll(query)[0]

    query = 'SELECT COUNT(*) FROM file_server fs LEFT OUTER JOIN server s ON fs.serverid = s.id WHERE s.id IS NULL OR NOT s.enabled'
    n_file_disabled_stale = conn.FileServer._connection.queryAll(query)[0]



    # file table
    n_file_total = conn.File.select().count()

    query = 'SELECT COUNT(*) FROM file f LEFT OUTER JOIN file_server fs ON f.id = fs.fileid WHERE fs.fileid IS NULL'
    n_file_stale = conn.File._connection.queryAll(query)[0]


    print 'file_server stale:            %10d' % n_file_stale
    print 'file_server stale/disabled:   %10d' % n_file_disabled_stale
    print
    print 'file total:                   %10d' % n_file_total
    print 'file stale:                   %10d' % n_file_stale
    print 


def vacuum(conn):
    """delete stale file entries from the database"""

    print 'Deleting stales from file_server...'
    query = 'DELETE FROM file_server WHERE serverid IN (SELECT id FROM server WHERE NOT enabled)'
    conn.FileServer._connection.query(query)

    print 'Deleting stales from file...'
    query = 'DELETE FROM file WHERE id IN (SELECT f.id FROM file f LEFT OUTER JOIN file_server fs ON f.id = fs.fileid WHERE fs.fileid IS NULL)'
    conn.File._connection.query(query)
    print 'Done.'

