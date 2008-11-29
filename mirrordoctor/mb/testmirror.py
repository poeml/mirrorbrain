
import urllib2

def access_http(url):
    r = urllib2.urlopen(url).read()
    print r


def head_req(url):

    req = urllib2.Request(url)
    req.get_method = lambda: "HEAD"

    try:
        response = urllib2.urlopen(req)
        return response.code
    except:
        return 0
