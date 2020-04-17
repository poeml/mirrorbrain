
def stale(conn, quietness):
    """show statistics about stale files in the database"""

    n_file_total = conn.Filearr.select().count()

    query = """SELECT count(*) FROM filearr
                   LEFT OUTER JOIN hash ON filearr.id = hash.file_id
               WHERE mirrors = '{}' AND hash.file_id IS NULL"""
    n_file_stale = conn.Filearr._connection.queryAll(query)[0]

    if quietness < 1:
        print('Total files:                     %10d' % n_file_total)
        print('Stale files (not on any mirror): %10d' % n_file_stale)


def vacuum(conn, quietness):
    """delete stale file entries from the database"""

    if quietness < 1:
        print('Deleting stale files...')
    query = """DELETE FROM filearr
               WHERE id IN (
                   SELECT filearr.id FROM filearr
                   LEFT OUTER JOIN hash ON filearr.id = hash.file_id
                   WHERE mirrors = '{}' AND hash.file_id IS NULL
               )"""
    conn.Filearr._connection.query(query)

    if quietness < 1:
        print('Done.')


def stats(conn):
    """show statistics about stale files in the database"""

    query = """SELECT relname, relkind, relfilenode, reltuples, relpages,
                      relpages*8 AS relKB
               FROM pg_class
               WHERE relkind IN ('r', 'i')
                      AND relname ~ '^.*(file|server|pfx|temp1|stats|hash).*'
               ORDER BY 1"""
    rows = conn.Filearr._connection.queryAll(query)

    print('Size(MB) Relation')
    total = 0
    for row in rows:
        name, kind, filenode, tuples, pages, size = row
        sizeMB = float(size) / 1024
        total += sizeMB
        print('%5.1f    %s' % (sizeMB, name))

    print('Total: %.1f' % total)


def shell(c):
    """spawn a database shell (psql).

    The argument is the configuration structure for that MirrorBrain instance."""
    import os
    os.environ['PGHOST'] = c.get('dbhost')
    os.environ['PGPORT'] = c.get('dbport', '5432')
    os.environ['PGUSER'] = c.get('dbuser')
    os.environ['PGPASSWORD'] = c.get('dbpass')
    os.environ['PGDATABASE'] = c.get('dbname')
    os.execlp('psql', 'psql')
