
def iplookup(conn, s):

    from mb.util import IpAddress
    import mb.mberr

    if s[0].isdigit():
        a = IpAddress()
        if ':' in s:
            a.ip6 = s
        else:
            a.ip = s

    else:
        import sys
        import socket
        ips = []
        ip6s = []
        try:
            for res in socket.getaddrinfo(s, None):
                af, socktype, proto, canonname, sa = res
                if af == socket.AF_INET6:
                    if sa[0] not in ip6s:
                        ip6s.append(sa[0])
                else:
                    if sa[0] not in ips:
                        ips.append(sa[0])
        except socket.error as e:
            if e[0] == socket.EAI_NONAME:
                raise mb.mberr.NameOrServiceNotKnown(s)
            else:
                print ('socket error msg:', str(e))
                return None

        # print (ips)
        # print (ip6s)
        if len(ips) > 1 or len(ip6s) > 1:
            print ('>>> warning: %r resolves to multiple IP addresses: ' % s, file=sys.stderr)
            if len(ips) > 1:
                print (', '.join(ips), file=sys.stderr)
            if len(ip6s) > 1:
                print (', '.join(ip6s), file=sys.stderr)
            print ('\n>>> see http://mirrorbrain.org/archive/mirrorbrain/0042.html why this could\n' \
                  '>>> could be a problem, and what to do about it. But note that this is not\n' \
                  '>>> necessarily a problem and could actually be intended depending on the\n' \
                  '>>> mirror\'s configuration (see http://mirrorbrain.org/issues/issue152).\n' \
                  '>>> It\'s best to talk to the mirror\'s admins.\n', file=sys.stderr)
        a = IpAddress()
        if ips:
            a.ip = ips[0]
        if ip6s:
            a.ip6 = ip6s[0]

    if not a.ip and not a.ip6:
        return a
    query = """SELECT pfx, asn \
                   FROM pfx2asn \
                   WHERE pfx >>= ipaddress('%s') \
                   ORDER BY @ pfx \
                   LIMIT 1"""

    if a.ip:
        try:
            res = conn.Pfx2asn._connection.queryAll(query % a.ip)
        except AttributeError:
            # we get this error if mod_asn isn't installed as well
            return a

    if len(res) == 1:
        (a.prefix, a.asn) = res[0]

    res = ""
    if a.ip6:
        try:
            res = conn.Pfx2asn._connection.queryAll(query % a.ip6)
        except ValueError:
            # we get this error if mod_asn isn't installed as well
            return a

    if len(res) == 1:
        (a.prefix6, a.asn6) = res[0]

    return a


def asn_prefixes(conn, asn):

    query = """SELECT pfx \
                   FROM pfx2asn \
                   WHERE asn='%s'""" % asn

    res = conn.Pfx2asn._connection.queryAll(query)
    l = [i[0] for i in res]
    return l
