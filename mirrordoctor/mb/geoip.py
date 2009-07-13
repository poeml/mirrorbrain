from subprocess import Popen, PIPE


def lookup_country_code(addr):
    out = Popen(['geoiplookup', addr], stdout=PIPE).communicate()[0]
    out = out.split(':')[1].strip().split(',')[0]

    return out.lower()


def lookup_region_code(addr):
    out = Popen(['geoiplookup_continent', addr], stdout=PIPE).communicate()[0]

    return out.strip().lower()


if __name__ == '__main__':
    import sys
    print 'country:', lookup_country_code(sys.argv[1])
    print 'region:', lookup_region_code(sys.argv[1])
