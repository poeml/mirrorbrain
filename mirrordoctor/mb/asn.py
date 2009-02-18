
def iplookup(conn, s):

    from mb.util import IpAddress


    if s[0].isdigit():
        a = IpAddress(s)

    else:
        import sys, socket
        # note the difference between socket.gethostbyname 
        # and socket.gethostbyname_ex
        host, aliases, ips = socket.gethostbyname_ex(s)

        #print host, aliases, ips
        if len(ips) != 1:
            print >>sys.stderr, \
                    'warning: %r resolves to a multiple IP addresses: %s' \
                    % (s, ', '.join(ips))
        a = IpAddress(ips[0])
        

    query = """SELECT pfx, asn \
                   FROM pfx2asn \
                   WHERE pfx >>= ip4r('%s') \
                   ORDER BY ip4r_size(pfx) \
                   LIMIT 1""" % a.ip

    res = conn.Pfx2asn._connection.queryAll(query)
    if len(res) != 1:
        return a
    (a.prefix, a.asn) = res[0]
    return a

def asn_prefixes(conn, asn):

    query = """SELECT pfx \
                   FROM pfx2asn \
                   WHERE asn='%s'""" % asn

    res = conn.Pfx2asn._connection.queryAll(query)
    l = [ i[0] for i in res ]
    return l
