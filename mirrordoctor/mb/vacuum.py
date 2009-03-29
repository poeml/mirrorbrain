
def stale(conn):
    """show statistics about stale files in the database"""

    n_file_total = conn.Filearr.select().count()

    query = "SELECT count(*) FROM filearr WHERE mirrors = '{}'"
    n_file_stale = conn.Filearr._connection.queryAll(query)[0]


    print 'Total files:                     %10d' % n_file_total
    print 'Stale files (not on any mirror): %10d' % n_file_stale


def vacuum(conn):
    """delete stale file entries from the database"""

    print 'Deleting stale files...'
    query = "DELETE FROM filearr WHERE mirrors = '{}'"
    conn.Filearr._connection.query(query)

    print 'Done.'

