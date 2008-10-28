
def stale(conn):
    """show statistics about stale files in the database"""

    # file_server table
    query = 'select count(*) from file_server as fs left outer join server as s on fs.serverid = s.id where isnull(s.id)'
    n_file_stale = conn.FileServer._connection.queryAll(query)[0]

    query = 'select count(*) from file_server as fs left outer join server as s on fs.serverid = s.id where isnull(s.id) or s.enabled = 0'
    n_file_disabled_stale = conn.FileServer._connection.queryAll(query)[0]



    # file table
    n_file_total = conn.File.select().count()

    query = 'select count(*) from file as f left outer join file_server as fs on f.id = fs.fileid where isnull(fs.fileid)'
    n_file_stale = conn.File._connection.queryAll(query)[0]


    print 'file_server stale:            %10d' % n_file_stale
    print 'file_server stale/disabled:   %10d' % n_file_disabled_stale
    print
    print 'file total:                   %10d' % n_file_total
    print 'file stale:                   %10d' % n_file_stale
    print 


def vacuum(conn):
    """delete stale file entries from the database"""

    print 'Deleting...'
    query = 'delete fs from file_server fs left outer join server s on fs.serverid = s.id where isnull(s.id) or s.enabled = 0'
    result = conn.FileServer._connection.queryAll(query)

    query = 'delete f from file as f left outer join file_server as fs on f.id = fs.fileid where isnull(fs.fileid)'
    result = conn.File._connection.queryAll(query)
    print 'Done.'

