# encoding: utf-8

import sys, os, os.path
import mb
import mb.files
import tempfile
import cgi

supported = ['txt', 'txt2', 'xhtml']


def is_odd(n):
    return (n % 2) == 1 


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

    # FIXME: make it configurable
    css = 'http://static.opensuse.org/css/mirrorbrain.css'

    html_head = """\
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN"
  "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
  <head>
<base href="http://narwal.opensuse.org/" />

    <meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
    <title>openSUSE Download Mirrors - Overview</title>
    <link type="text/css" rel="stylesheet" href="%(css)s" />
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
    table_start = """\
  <table summary="List of all mirrors">
    <caption>All mirrors</caption>
"""
    table_end = """
    </tbody>
  </table>
"""
    row_start, row_end = '    <tr%s>\n', '    </tr>\n'

    table_header_start_template = """\
    <thead>
      <tr>
        <th scope="col">Country</th>
        <th scope="col">Operator</th>
        <th scope="col" colspan="3">Mirror URL</th>
        <th scope="col">Priority</th>
"""
    table_header_end_template = """\
      </tr>
    </thead>

    <tbody>
"""
    row_template = """\
      <td> <img src="%(img_link)s" 
                width="16" height="11" alt="%(country_code)s" /> %(country_name)s</td>
      <td><a href="%(operatorUrl)s">%(operatorName)s</a></td>
      <td>%(http_link)s</td>
      <td>%(ftp_link)s</td>
      <td>%(rsync_link)s</td>
      <td>%(prio)s</td>
"""


    region_name = dict()
    for i in conn.Region.select():
        region_name[i.code] = i.name
    
    row_class = lambda x: is_odd(x) and ' class="odd"' or ''

    # FIXME: we show FTP URLs as HTTP URLs if they are entered as such.
    # maybe suppress them if (HTTP != 'http://')
    # example: yandex.ru
    href = lambda x, y: x and '<a href="%s">%s</a>' % (x, y)  or '' # 'n/a'

    def imgref(country_code):
        if not opts.inline_images_from:
            return 'flags/%s.%s' % (country_code, opts.image_type)
        else:
            return mb.util.data_url(opts.inline_images_from, 
                                    country_code + '.' + opts.image_type)


    last_region = 'we have not started yet...'

    yield html_head % { 'css': css }
    yield table_start
    yield table_header_start_template
    for marker in markers:
        yield '        <th scope="col">%s</th>' % marker.subtreeName
    yield table_header_end_template
    try:
        # if this doesn't work, ...
        markers_cnt = markers.count()
    except:
        # ... we have a filtered list of database result objects
        markers_cnt = len(markers)

    row_cnt = 0
    for mirror in mirrors:
        row_cnt += 1
        region = mirror.region.lower()
        if region != last_region:
            # new region block
            #if last_region != 'we have not started yet...':
            #    yield table_end
            #
            #yield '\n\n<h2>Mirrors in %s:</h2>\n' % region_name[region]
            yield row_start % row_class(0)
            yield '<td colspan="%s">      Mirrors in %s:</td>\n' \
                     % (6 + markers_cnt, region_name[region])
            yield row_end
        last_region = region

        country_name = conn.Country.select(
                conn.Country.q.code == mirror.country.lower())[0].name
        map = { 'country_code': mirror.country.lower(),
                'country_name': country_name,
                'img_link':   imgref(mirror.country.lower()),
                'region':     region,
                'identifier': mirror.identifier,
                'operatorName': cgi.escape(mirror.operatorName),
                'operatorUrl': mirror.operatorUrl,
                'http_link':  href(mirror.baseurl, 'HTTP'),
                'ftp_link':   href(mirror.baseurlFtp, 'FTP'),
                'rsync_link': href(mirror.baseurlRsync, 'rsync'),
                'prio':       mirror.score,
                }
        

        row = []
        row.append(row_start % row_class(row_cnt))
        row.append(row_template % map)
        
        empty = True
        for marker in markers:
            if mb.files.check_for_marker_files(conn, marker.markers, mirror.id):
                #row.append('    <td>√</td>')
                row.append('      <td>&radic;</td>\n')
                empty = False
            else:
                row.append('      <td></td>\n')

        row.append(row_end)

        if not opts.skip_empty or not empty:
            yield ''.join(row)

    yield table_end

    yield html_foot
