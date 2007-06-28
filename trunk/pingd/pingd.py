#!/usr/bin/python

__author__ = "Peter Poeml <poeml@suse.de>"
__copyright__ = "Novell Inc."
__license__ = "GPL"

import sys, os, os.path, time, threading, socket, urllib2, httplib
import logging, logging.handlers
from optparse import OptionParser
import ConfigParser, MySQLdb

LOGLEVEL = 'INFO'
USER_AGENT = 'pingd/openSUSE (see http://en.opensuse.org/Build_Service/Redirector)'
LOGFORMAT = '%(asctime)s %(levelname)-8s %(message)s'
DATEFORMAT = '%b %d %H:%M:%S'


def ping_http(mirror):
    """Try to reach host at baseurl. 
    Set status_baseurl_new."""

    logging.debug("%s pinging %s" % (threading.currentThread().getName(), mirror['identifier']))

    #req = urllib2.Request('http://old-cherry.suse.de') # never works
    #req = urllib2.Request('http://doozer.poeml.de/')   # always works
    req = urllib2.Request(mirror['baseurl'])

    req.add_header('User-Agent', USER_AGENT)
    req.get_method = lambda: "HEAD"

    mirror['status_baseurl_new'] = False
    mirror['timed_out'] = True

    try:
        response = urllib2.urlopen(req)
        logging.debug('%s got response for %s: %s' % (threading.currentThread().getName(), mirror['identifier'], response))
        try:
            mirror['response_code'] = response.code
            # if the web server redirects to an ftp:// URL, our response won't have a code attribute
            # (except we are going via a proxy)
        except AttributeError:
            if response.url.startswith('ftp://'):
                # count as success
                mirror['response_code'] = 200
            logging.debug('mirror %s redirects to ftp:// URL' % mirror['identifier'])
        mirror['response'] = response.read()
        mirror['status_baseurl_new'] = True

    except httplib.BadStatusLine:
        mirror['response_code'] = None
        mirror['response'] = None
        
    except urllib2.HTTPError, e:
        mirror['response_code'] = e.code
        mirror['response'] = e.read()

    except urllib2.URLError, e:
        mirror['response_code'] = 0
        mirror['response'] = "%s" % e.reason


    # not reached, if the timeout goes off
    mirror['timed_out'] = False


def main():

    #
    # read config file
    #
    conffile = os.path.expanduser('~/.pingdrc')
    cp = ConfigParser.SafeConfigParser()
    cp.read(conffile)
    config = dict(cp.items('general'))
    LOGLEVEL = config.get('loglevel', 'INFO')
    LOGFILE = config.get('logfile', '/var/log/pingd')
    MAILTO = config.get('mailto', 'root@localhost')

    #
    # parse commandline
    #
    parser = OptionParser(usage="%prog [options] [<mirror identifier>+]", version="%prog 1.0")

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
                      default=30,
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

    socket.setdefaulttimeout(options.timeout)

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
    mail = logging.handlers.SMTPHandler('localhost', 
                                        'root@' + socket.gethostbyaddr(socket.gethostname())[0], 
                                        options.mailto,
                                        'pingd warning')
    mail.setLevel(logging.WARNING)
    mailformatter = logging.Formatter(LOGFORMAT, DATEFORMAT)
    mail.setFormatter(mailformatter)
    logging.getLogger('').addHandler(mail)


    #
    # prepare SQL statements
    #
    sql_select_raw = 'select id, identifier, baseurl, status_baseurl, enabled from %s where baseurl != ""' % config['dbtable']
    sql_update_raw = 'update %s set status_baseurl=%%s where id=%%s' % config['dbtable']
    sql_enable_raw = 'update %s set enabled=%%s, comment=%%s where id=%%s' % config['dbtable']
    sql_select_comment_raw = 'select comment from %s where id=%%s' % config['dbtable']
    sql_enabled_raw = 'update %s set enabled=%%s, comment=%%s where id=%%s' % config['dbtable']

    #
    # get mirrors from database
    #
    mirrors = []
    dbh = MySQLdb.connect(config['dbhost'], config['dbuser'], config['dbpass'], config['dbname'])
    cursor = dbh.cursor(MySQLdb.cursors.DictCursor)
    cursor.execute(sql_select_raw)
    if not args:
        for mirror in cursor.fetchall():
            if mirror['enabled'] == 1:
                mirrors.append(mirror)
    else:
        for mirror in cursor.fetchall():
            if mirror['identifier'] in args:
                mirrors.append(mirror)
    if not mirrors:
        sys.exit('no mirrors found')

    #
    # start work
    #
    logging.info('----- checking %s mirrors' % len(mirrors))

    for i, mirror in enumerate(mirrors):

        t = threading.Thread(target=ping_http, 
                             args=[mirrors[i]], 
                             name="pingThread-%s" % mirror['id'])
        # thread will keep the program from terminating.
        t.setDaemon(0)
        t.start()

    while threading.activeCount() > 1:
        logging.debug('waiting for %s threads to exit' % (threading.activeCount() - 1))
        time.sleep(1)

    for mirror in mirrors:

        # old failure
        if not mirror['status_baseurl'] and not mirror['status_baseurl_new']:

            if mirror['response_code'] and (mirror['response_code'] != 200):
                logging.warning("""%(identifier)s: (%(baseurl)s): response code not 200: %(response_code)s: %(response)s

Disabling. 
Manual enabling will be needed.
Use pingd.py -e <identifier>""" % mirror)

                cursor.execute(sql_select_comment_raw, (mirror['id']))
                comment = cursor.fetchone()['comment'] or ''
                comment += (' *** set enabled=0 by pingd at %s due to status code %s' % (time.ctime(), mirror['response_code']))
                logging.debug(sql_enabled_raw % (0, comment, mirror['id']))
                cursor.execute(sql_enabled_raw, (0, comment, mirror['id']))

            logging.debug('still dead: %(identifier)s (%(baseurl)s): %(response_code)s: %(response)s' % mirror)

        # alive
        elif mirror['status_baseurl'] and mirror['status_baseurl_new']:
            logging.debug('alive: %(identifier)s: %(response)s' % mirror)

        # alive, but status not OK
        elif mirror['status_baseurl'] and mirror['status_baseurl_new']:
            if mirror['response_code'] != 200:
                logging.warning('%(identifier)s: (%(baseurl)s): response code not 200: %(response_code)s: %(response)s' % mirror)
                logging.debug(sql_enabled_raw % (0, mirror['id']))
                if not options.no_run:
                    cursor.execute(sql_enabled_raw, (0, mirror['id']))

        # new failure
        elif not mirror['status_baseurl_new'] and mirror['status_baseurl']:
            logging.info('FAIL: %(identifier)s (%(baseurl)s): %(response)s' % mirror)
            logging.debug(sql_update_raw % (0, mirror['id']))
            if not options.no_run:
                cursor.execute(sql_update_raw, (0, mirror['id']))

        # revived
        elif not mirror['status_baseurl'] and mirror['status_baseurl_new'] == 1:
            logging.info('REVIVED: %s' % mirror['identifier'])
            logging.debug(sql_update_raw % (1, mirror['id']))
            if not options.no_run:
                cursor.execute(sql_update_raw, (1, mirror['id']))
            if options.enable_revived:
                logging.info('re-enabling %s' % mirror['identifier'])
                if not options.no_run:
                    cursor.execute(sql_select_comment_raw, (mirror['id']))
                    comment = cursor.fetchone()['comment'] or ''
                    comment = comment[:comment.find('*** ')]
                    cursor.execute(sql_enable_raw, (1, comment, mirror['id']))




if __name__ == '__main__':
    main()
