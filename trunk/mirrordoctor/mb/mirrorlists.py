# encoding: utf-8

import sys, os, os.path
import mb
import mb.files
import tempfile

supported = ['txt', 'txt2', 'xhtml']


def genlist(conn, opts, mirrors, markers, format='txt2'):
    if format == 'txt':
        gen = txt(conn, opts, mirrors, markers)
    elif format == 'txt2':
        gen =txt2(conn, opts, mirrors, markers)
    elif format == 'xhtml':
        gen = xhtml(conn, opts, mirrors, markers)

    if not opts.output:
        for i in gen:
            sys.stdout.write(i + '\n')

    if opts.output:
        fname = opts.output
        # make a tempfile because we might write in a directory not owned by
        # root. Also we can then atomically move the tempfile over an existing file,
        # without the risk that the webserver sends out incomplete content.
        (fd, tmpfname) = tempfile.mkstemp(prefix = '.' + os.path.basename(fname),
                                          dir = os.path.dirname(fname))
        os.chmod(tmpfname, 0644)
        f = os.fdopen(fd, 'w')

        for i in gen:
            f.write(i + '\n')

        f.close()
        try:
            os.rename(tmpfname, fname)
        except:
            print >>sys.stderr, 'could not rename file'
            os.unlink(tmpfname)
            raise



def txt(conn, opts, mirrors, markers):
    for mirror in mirrors:
        yield ''
        yield mirror.identifier
        #yield mirror.identifier, mirror.baseurl, mirror.baseurlFtp, mirror.baseurlRsync, mirror.score

        for marker in markers:
            if mb.files.check_for_marker_files(conn, marker.markers, mirror.id):
                yield '+' + marker.subtreeName
            else:
                yield '-' + marker.subtreeName


def txt2(conn, opts, mirrors, markers):
    for mirror in mirrors:
        for marker in markers:
            if mb.files.check_for_marker_files(conn, marker.markers, mirror.id):
                yield '%s: %s' % (mirror.identifier, marker.subtreeName)


def xhtml(conn, opts, mirrors, markers):

    if opts.inline_images_from:
        import os
        import mb.util
        if not os.path.exists(opts.inline_images_from):
            sys.exit('path %r does not exist' % opts.inline_images_from)

    html_head = """\
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN"
  "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
  <head>
<base href="http://narwal.opensuse.org/" />

    <meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
    <title>openSUSE Download Mirrors - Overview</title>
    <link type="text/css" rel="stylesheet" href="/css/mirrorbrain.css" />
    <link href="/favicon.ico" rel="shortcut icon" />
  
    <meta http-equiv="Language" content="en" />
    <meta name="description" content="openSUSE Download Mirrors" />
    <meta name="keywords" content="openSUSE download metalink redirector mirror mirrors" />
    <meta name="author" content="openSUSE project" />
    <meta name="robots" content="index, nofollow" />
  </head>

  <body>
"""

    html_foot = """\

  </body>
</html>
"""
    table_start, table_end = '<table>', '</table>'
    row_start, row_end = '  <tr>', '  </tr>'

    table_header_template = """\
    <th>Country</th>
    <th>Mirror</th>
    <th colspan="3">URL</th>
    <th>Priority</th>
"""
    row_template = """\
    <td><img src="%(img_link)s" width="16" height="11" alt="%(country_code)s" />
        %(country_name)s
    </td>
    <td><a href="%(operatorUrl)s">%(operatorName)s</a></td>
    <td>%(http_link)s</td>
    <td>%(ftp_link)s</td>
    <td>%(rsync_link)s</td>
    <td>%(prio)s</td>
"""


    region_name = dict(af='Africa', as='Asia', eu='Europe', na='North America', sa='South America', oc='Oceania')
    
    href = lambda x, y: x and '<a href="%s">%s</a>' % (x, y)  or '' # 'n/a'

    def imgref(country_code):
        if not opts.inline_images_from:
            return 'flags/%s.%s' % (country_code, opts.image_type)
        else:
            return mb.util.data_url(opts.inline_images_from, 
                                    country_code + '.' + opts.image_type)


    last_region = 'we have not started yet...'

    yield html_head

    for mirror in mirrors:
        region = mirror.region.lower()
        if region != last_region:
            # new region block
            if last_region != 'we have not started yet...':
                yield table_end

            yield '\n\n<h2>Mirrors in %s:</h2>\n' % region_name[region]
            yield table_start
            yield row_start
            yield table_header_template
            for marker in markers:
                yield '    <th>%s</th>' % marker.subtreeName
            yield row_end
        last_region = region

        country_name = conn.Country.select(
                conn.Country.q.code == mirror.country.lower())[0].name
        map = { 'country_code': mirror.country.lower(),
                'country_name': country_name,
                'img_link':   imgref(mirror.country.lower()),
                'region':     region,
                'identifier': mirror.identifier,
                'operatorName': mirror.operatorName,
                'operatorUrl': mirror.operatorUrl,
                'http_link':  href(mirror.baseurl, 'HTTP'),
                'ftp_link':   href(mirror.baseurlFtp, 'FTP'),
                'rsync_link': href(mirror.baseurlRsync, 'rsync'),
                'prio':       mirror.score,
                }
        
        yield row_start
        yield row_template % map
        
        for marker in markers:
            if mb.files.check_for_marker_files(conn, marker.markers, mirror.id):
                #yield '    <td>√</td>'
                yield '    <td>&radic;</td>'
            else:
                yield '    <td> </td>'

        yield row_end

    yield table_end

    yield html_foot
