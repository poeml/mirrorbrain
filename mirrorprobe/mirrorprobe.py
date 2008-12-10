#!/usr/bin/python

__author__ = "Peter Poeml <poeml@suse.de>"
__copyright__ = "Novell Inc."
__license__ = "GPL"

import sys, os, os.path, time, threading, socket, urllib2, httplib
import logging, logging.handlers
from optparse import OptionParser
from sqlobject import *
from sqlobject.sqlbuilder import AND

USER_AGENT = 'MirrorBrain Probe (see http://mirrorbrain.org/probe_info)'

def reenable(mirror):
    comment = mirror.comment or ''
    comment += ('\n\n*** reenabled by mirrorprobe at %s.' % (time.ctime()))
    mirror.comment = comment
    mirror.enabled = 1


def probe_http(mirror):
    """Try to reach host at baseurl. 
    Set status_baseurl_new."""

    logging.debug("%s probing %s" % (threading.currentThread().getName(), mirror.identifier))

    #req = urllib2.Request('http://old-cherry.suse.de') # never works
    #req = urllib2.Request('http://doozer.poeml.de/')   # always works
    req = urllib2.Request(mirror.baseurl)

    req.add_header('User-Agent', USER_AGENT)
    #req.get_method = lambda: "HEAD"

    mirror.status_baseurl_new = False
    mirror.timed_out = True
    mirror.response_code = None
    mirror.response = None

    if mirror.baseurl == '':
        return None

    try:
        response = urllib2.urlopen(req)
        try:
            mirror.response_code = response.code
            # if the web server redirects to an ftp:// URL, our response won't have a code attribute
            # (except we are going via a proxy)
        except AttributeError:
            if response.url.startswith('ftp://'):
                # count as success
                mirror.response_code = 200
            logging.debug('mirror %s redirects to ftp:// URL' % mirror.identifier)
        logging.debug('%s got response for %s: %s' % (threading.currentThread().getName(), mirror.identifier, getattr(response, 'code', None)))

        try:
            mirror.response = response.read()
        except ValueError, e:
            if str(e).startswith('invalid literal for int()'):
                mirror.response = 'response not read due to http://bugs.python.org/issue1205'
                logging.info('mirror %s sends broken chunked reply, see http://bugs.python.org/issue1205' % mirror.identifier)
            else:
                raise
        mirror.status_baseurl_new = True

    except httplib.BadStatusLine:
        mirror.response_code = None
        mirror.response = None
        
    except urllib2.HTTPError, e:
        mirror.response_code = e.code
        mirror.response = e.read()

    except urllib2.URLError, e:
        mirror.response_code = 0
        mirror.response = "%s" % e.reason

    except IOError, e:
        # IOError: [Errno ftp error] (111, 'Connection refused')
        print e.errno
        if e.errno == 'ftp error':
            mirror.response_code = 0
            mirror.response = "%s" % e.errno
            #logging.info('mirror %s gives an "ftp error"' % mirror.identifier)
        else:
            raise


    # not reached, if the timeout goes off
    mirror.timed_out = False


def main():

    #
    # read config file
    #
    brain_instance = None
    if '-b' in sys.argv:
        brain_instance = sys.argv[sys.argv.index('-b') + 1]

    import mb.conf
    config = mb.conf.Config(instance = brain_instance)


    LOGLEVEL = config.mirrorprobe.get('loglevel', 'INFO')
    LOGFILE = config.mirrorprobe.get('logfile', '/var/log/mirrorprobe.log')
    MAILTO = config.mirrorprobe.get('mailto', 'root@localhost')

    #
    # parse commandline
    #
    parser = OptionParser(usage="%prog [options] [<mirror identifier>+]", version="%prog 1.0")

    parser.add_option("-b", "--brain-instance",
                      dest="brain_instance",
                      default=None,
                      help="name of the MirrorBrain instance to be used",
                      metavar="NAME")

    parser.add_option("-l", "--log",
                      dest="logfile",
                      default=LOGFILE,
                      help="path to logfile",
                      metavar="LOGFILE")

    parser.add_option("-L", "--loglevel",
                      dest="loglevel",
                      default=LOGLEVEL,
                      help="Loglevel (DEBUG, INFO, WARNING, ERROR, CRITICAL)",
                      metavar="LOGLEVEL")

    parser.add_option("-T", "--mailto",
                      dest="mailto",
                      default=MAILTO,
                      help="email adress to mail warnings to",
                      metavar="EMAIL")

    parser.add_option("-t", "--timeout",
                      dest="timeout",
                      default=60,
                      help="Timeout in seconds",
                      metavar="TIMEOUT")

    parser.add_option("-n", "--no-run",
                      dest="no_run", 
                      default=False,
                      action="store_true", 
                      help="don't update the database. Only look")

    parser.add_option("-e", "--enable-revived",
                      dest="enable_revived", 
                      default=False,
                      action="store_true", 
                      help="enable revived servers")

    (options, args) = parser.parse_args()

    socket.setdefaulttimeout(int(options.timeout))


    # an "are you alive check" is relatively useless if it is answered by an intermediate cache
    for i in ['http_proxy', 'HTTP_PROXY', 'ftp_proxy', 'FTP_PROXY']:
        if i in os.environ:
            del os.environ[i]

    LOGFORMAT = '%(asctime)s ' + config.instance + ' %(levelname)-8s %(message)s'
    DATEFORMAT = '%b %d %H:%M:%S'

    #
    # set up logging
    #
    # to file
    logging.basicConfig(level=logging.getLevelName(options.loglevel),
                        format=LOGFORMAT,
                        datefmt=DATEFORMAT,
                        filename=options.logfile,
                        filemode='a')
    # to console
    console = logging.StreamHandler()
    console.setLevel(logging.getLevelName(options.loglevel))
    formatter = logging.Formatter(LOGFORMAT, DATEFORMAT)
    console.setFormatter(formatter)
    logging.getLogger('').addHandler(console)

    # warnings will be mailed
    toaddrs = [ i.strip() for i in options.mailto.split(',') ]
    mail = logging.handlers.SMTPHandler('localhost', 
                                        'root@' + socket.gethostbyaddr(socket.gethostname())[0], 
                                        toaddrs,
                                        '[%s] mirrorprobe warning' % config.instance)
    mail.setLevel(logging.WARNING)
    mailformatter = logging.Formatter(LOGFORMAT, DATEFORMAT)
    mail.setFormatter(mailformatter)
    logging.getLogger('').addHandler(mail)


    #
    # setup database connection
    #
    import mb.conn
    conn = mb.conn.Conn(config.dbconfig, 
                        debug = (options.loglevel == 'DEBUG'))


    #
    # get mirrors from database
    #
    mirrors = []
    if args:
        # select all mirrors matching the given identifiers
        result = conn.Server.select()
        for i in result:
            if i.identifier in args:
                mirrors.append(i)
    else:
        # select all enabled mirrors
        #
        # ignore wildcard mirrors, assuming that they can't be checked by normal means (i.e., baseurl itself may
        # not give a 200. Just some files are served maybe...
        result = conn.Server.select(AND(conn.Server.q.enabled == 1, conn.Server.q.country != '**'))
        for i in result:
            mirrors.append(i)

    if not mirrors:
        sys.exit('no mirrors found')

    #
    # start work
    #
    logging.info('----- %s mirrors to check' % len(mirrors))

    for i, mirror in enumerate(mirrors):
        #mirror.status_baseurl_new = False
        #mirror.timed_out = True

        t = threading.Thread(target=probe_http, 
                             args=[mirrors[i]], 
                             name="probeThread-%s" % mirror.id)
        # thread will keep the program from terminating.
        t.setDaemon(0)
        t.start()

    while threading.activeCount() > 1:
        logging.debug('waiting for %s threads to exit' % (threading.activeCount() - 1))
        time.sleep(1)


    for mirror in mirrors:

        logging.debug('%s: baseurl %s / baseurl_new %s' % (mirror.identifier, repr(mirror.statusBaseurl), repr(mirror.status_baseurl_new)))
        # old failure
        if not mirror.statusBaseurl and not mirror.status_baseurl_new:

            if mirror.response_code and (mirror.response_code != 200):
                logging.warning("""%s: (%s): response code not 200: %s: %s

Disabling. 
Manual enabling will be needed.
Use mirrorprobe.py -e <identifier>

And, if the mirror has been disabled for a while, scan it before enabling
it again!
""" % (mirror.identifier, mirror.baseurl, mirror.response_code, mirror.response))

                comment = mirror.comment or ''
                comment += ('\n\n*** set enabled=0 by mirrorprobe at %s due to status code %s' % (time.ctime(), mirror.response_code))
                logging.debug('setting enabled=0 for %s' % (mirror.identifier))
                if not options.no_run:
                    mirror.enabled = 0
                    mirror.comment = comment

            logging.debug('still dead: %s (%s): %s: %s' % (mirror.identifier, mirror.baseurl, mirror.response_code, mirror.response))

        # alive
        elif mirror.statusBaseurl and mirror.status_baseurl_new:
            logging.debug('alive: %s: %s' % (mirror.identifier, mirror.response))
            if mirror.enabled == 0 and options.enable_revived:
                logging.info('re-enabling %s' % mirror.identifier)
                if not options.no_run:
                    reenable(mirror)

        # new failure
        elif not mirror.status_baseurl_new and mirror.statusBaseurl:
            logging.info('FAIL: %s (%s): %s' % (mirror.identifier, mirror.baseurl, mirror.response))
            logging.debug('setting status_baseurl=0 for %s (id=%s)' % (mirror.identifier, mirror.id))
            if not options.no_run:
                mirror.statusBaseurl = 0

        # revived
        elif not mirror.statusBaseurl and mirror.status_baseurl_new == 1:
            logging.info('REVIVED: %s' % mirror.identifier)
            logging.debug('setting status_baseurl=1 for %s (id=%s)' % (mirror.identifier, mirror.id))
            if not options.no_run:
                mirror.statusBaseurl = 1
            if options.enable_revived:
                logging.info('re-enabling %s' % mirror.identifier)
                if not options.no_run:
                    reenable(mirror)




if __name__ == '__main__':
    main()
