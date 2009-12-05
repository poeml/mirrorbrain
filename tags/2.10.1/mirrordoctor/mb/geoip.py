import sys
import os
from subprocess import Popen, PIPE
import errno

# try different databases and different locations
databases = ['/var/lib/GeoIP/GeoLiteCity.dat.updated', 
             '/var/lib/GeoIP/GeoLiteCity.dat', 
             '/var/lib/GeoIP/GeoIP.dat.updated',
             '/var/lib/GeoIP/GeoIP.dat',
             '/usr/share/GeoIP/GeoLiteCity.dat.updated', 
             '/usr/share/GeoIP/GeoLiteCity.dat', 
             '/usr/share/GeoIP/GeoIP.dat.updated',
             '/usr/share/GeoIP/GeoIP.dat',
             ]
for i in databases:
    if os.path.exists(i):
        database = i


def lookup_country_code(addr):
    out = Popen(['geoiplookup', '-f', database, addr], stdout=PIPE).communicate()[0]
    out = out.split(':')[1].strip().split(',')[0]

    return out.lower()


def lookup_region_code(addr):
    try:
        out = Popen(['geoiplookup_continent', '-f', database, addr], stdout=PIPE).communicate()[0]
    except OSError, e:
        if e.errno == errno.ENOENT:
            sys.exit('Error: The geoiplookup_continent binary could not be found.\n'
                     'Make sure to install the geoiplookup_continent into a directory contained in $PATH.')

    return out.strip().lower()


if __name__ == '__main__':
    import sys
    print 'country:', lookup_country_code(sys.argv[1])
    print 'region:', lookup_region_code(sys.argv[1])