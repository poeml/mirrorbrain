
import urllib2

def access_http(url):
    r = urllib2.urlopen(url).read()
    print r

