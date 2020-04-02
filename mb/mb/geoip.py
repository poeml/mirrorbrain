import sys
import os
from subprocess import Popen, PIPE
import errno
from mb.util import af_from_string

ENV = {'PATH': ':'.join([os.getenv('PATH'), '/usr/share/mirrorbrain'])}

database = None
database6 = None

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
        break
databases6 = ['/var/lib/GeoIP/GeoIPv6.dat.updated',
              '/var/lib/GeoIP/GeoIPv6.dat',
              '/usr/share/GeoIP/GeoIPv6.dat.updated',
              '/usr/share/GeoIP/GeoIPv6.dat',
              ]
for i in databases6:
    if os.path.exists(i):
        database6 = i
        break


def lookup_country_code(addr):
    if af_from_string(addr) == 10:
        out = Popen(['geoiplookup6', '-f', database6, addr],
                    env=ENV, stdout=PIPE).communicate()[0]
    else:
        out = Popen(['geoiplookup', '-f', database, addr],
                    env=ENV, stdout=PIPE).communicate()[0]
    out = out.split(':')[1].strip().split(',')[0]

    return out.lower()


def lookup_region_code(addr):
    try:
        out = Popen(['geoiplookup_continent', '-f', database, addr],
                    env=ENV, stdout=PIPE).communicate()[0]
    except OSError as e:
        if e.errno == errno.ENOENT:
            sys.exit('Error: The geoiplookup_continent binary could not be found.\n'
                     'Make sure to install the geoiplookup_continent into a directory contained in $PATH.')

    return out.strip().lower()


def lookup_coordinates(addr):
    try:
        out = Popen(['geoiplookup_city', '-f', database, addr],
                    env=ENV, stdout=PIPE).communicate()[0]
    except OSError as e:
        if e.errno == errno.ENOENT:
            sys.exit('Error: The geoiplookup_city binary could not be found.\n'
                     'Make sure to install the geoiplookup_city into a directory contained in $PATH.')

    lat = lng = 0
    for line in out.splitlines():
        if line.startswith('Latitude'):
            lat = float(line.split()[1])
            continue
        if line.startswith('Longitude'):
            lng = float(line.split()[1])
            continue

    # if the number of digits here matches the database declaration, we can
    # compare the values and see whether they have changed
    lat = round(lat, 3)
    lng = round(lng, 3)
    return lat, lng


if __name__ == '__main__':
    import sys
    print ('country:', lookup_country_code(sys.argv[1]))
    print ('region:', lookup_region_code(sys.argv[1]))
