# encoding: utf-8
import sys
import os
import os.path
import mb
import mb.util
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
        gen = txt2(conn, opts, mirrors, markers)
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
        (fd, tmpfname) = tempfile.mkstemp(prefix='.' + os.path.basename(fname),
                                          dir=os.path.dirname(fname))
        try:
            os.chmod(tmpfname, 0o0644)
            f = os.fdopen(fd, 'w')

            for i in gen:
                f.write(i + '\n')

            f.close()
            try:
                os.rename(tmpfname, fname)
            except:
                print('could not rename file', file=sys.stderr)
                raise
        finally:
            if os.path.exists(tmpfname):
                os.unlink(tmpfname)


def txt(conn, opts, mirrors, markers):
    for mirror in mirrors:
        yield ''
        yield mirror.identifier
        # yield mirror.identifier, mirror.baseurl, mirror.baseurlFtp, mirror.baseurlRsync, mirror.score

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
        if not os.path.exists(opts.inline_images_from):
            sys.exit('path %r does not exist' % opts.inline_images_from)

    html_header = """\
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN"
  "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
  <head>
    <base href="http://mirrorbrain.org/" />

    <meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
    <title>%(title)s</title>
    <link type="text/css" rel="stylesheet" href="/mirrorbrain.css" />
    <link href="/favicon.ico" rel="shortcut icon" />

    <meta http-equiv="Language" content="en" />
    <meta name="description" content="Download Mirrors" />
    <meta name="keywords" content="download metalink redirector mirror mirrors" />
    <meta name="author" content="MirrorBrain" />
    <meta name="robots" content="index, nofollow" />
  </head>

  <body>

"""

    html_footer = """\

  <address>
  Generated %(utc)s by <a href="http://mirrorbrain.org/">MirrorBrain</a>
  </address>
  </body>
</html>
"""

    table_start = """\
  <table summary="List of all mirrors">
    <caption>%(caption)s</caption>
"""
    table_col_defs = """\
    <col id="country" />
    <col id="operator" />
    <col id="http" />
    <col id="ftp" />
    <col id="rsync" />
    <col id="prio" />
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
        <th scope="col" colspan="3">Mirror URLs</th>
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

    import time
    utc = time.strftime("%a, %d %b %Y %H:%M:%S UTC", time.gmtime())

    def row_class(x): return is_odd(x) and ' class="odd"' or ''
    def col_class(x): return is_odd(x) and 'a' or 'b'
    #odd_id = lambda x: is_odd(x) and ' id="odd"' or ''

    def stars(x): return x < 50 and '*' \
        or x >= 50 and x < 100 and '**' \
        or x == 100 and '***' \
        or x > 100 and '****'

    region_name = dict()
    for i in conn.Region.select():
        region_name[i.code] = i.name

    # FIXME: we show FTP URLs as HTTP URLs if they are entered as such.
    # maybe suppress them if (HTTP != 'http://')
    # example: yandex.ru
    def href(x, y): return x and '<a href="%s">%s</a>' % (x, y) or ''  # 'n/a'

    def imgref(country_code):
        if not opts.inline_images_from:
            return 'flags/%s.%s' % (country_code, opts.image_type)
        else:
            return mb.util.data_url(opts.inline_images_from,
                                    country_code + '.' + opts.image_type)

    last_region = 'we have not started yet...'

    try:
        # if the following doesn't work, ...
        markers_cnt = markers.count()
    except:
        # ... then we are dealing with a filtered list of database result objects
        markers_cnt = len(markers)

    if opts.html_header:
        html_header = open(opts.html_header).read()
    if opts.html_footer:
        html_footer = open(opts.html_footer).read()

    yield html_header % {'title': opts.title or 'Download Mirrors - Overview',
                         'utc': utc}
    yield table_start % {'caption': opts.caption or 'All mirrors'}
    yield table_col_defs
    for i in range(1, markers_cnt + 1):
        # yield '    <col id="subtree%i" />' % i
        # yield '    <col%s />' % odd_id(i)
        # col ids need to be unique, thus we can't use them for an odd/even attribute.
        yield '    <col />'

    yield table_header_start_template
    col_cnt = 0
    for marker in markers:
        col_cnt += 1
        yield '        <th scope="col" class="%s">%s</th>' \
            % (col_class(col_cnt), marker.subtreeName)
    yield table_header_end_template

    row_cnt = 0
    for mirror in mirrors:
        row_cnt += 1
        region = mirror.region.lower()
        if region != last_region:
            # new region block
            # if last_region != 'we have not started yet...':
            #    yield table_end
            #
            # yield '\n\n<h2>Mirrors in %s:</h2>\n' % region_name[region]
            yield row_start % row_class(0)
            # yield '<td colspan="%s" class="newregion">      Mirrors in %s:</td>\n' \
            yield '      <td colspan="%s" class="newregion">%s:</td>\n' \
                % (6 + markers_cnt, region_name[region])
            yield row_end
        last_region = region

        if row_cnt % 20 == 0:
            yield row_start % row_class(0)
            yield '      <td colspan="%s"></td>\n' % 6
            col_cnt = 0
            for marker in markers:
                col_cnt += 1
                yield '      <td class="%s">%s</td>\n' \
                    % (col_class(0), marker.subtreeName)
                # % (col_class(col_cnt), marker.subtreeName)
            yield row_end

        country_name = conn.Country.select(
            conn.Country.q.code == mirror.country.lower())[0].name
        map = {'country_code': mirror.country.lower(),
               'country_name': country_name,
               'img_link':   imgref(mirror.country.lower()),
               'region':     region,
               'identifier': mirror.identifier,
               'operatorName': cgi.escape(mirror.operatorName),
               'operatorUrl': mirror.operatorUrl,
               'http_link':  href(mb.util.strip_auth(mirror.baseurl), 'HTTP'),
               'ftp_link':   href(mb.util.strip_auth(mirror.baseurlFtp), 'FTP'),
               'rsync_link': href(mb.util.strip_auth(mirror.baseurlRsync), 'rsync'),
               'prio':       stars(mirror.score),
               }

        row = []
        row.append(row_start % row_class(row_cnt))
        row.append(row_template % map)

        empty = True
        col_cnt = 0
        for marker in markers:
            col_cnt += 1
            if mb.files.check_for_marker_files(conn, marker.markers, mirror.id):
                #checkmark = '√'
                checkmark = '&radic;'
                empty = False
            else:
                #checkmark = '-'
                checkmark = ''
            row.append('      <td class="%s">%s</td>\n'
                       % (col_class(col_cnt), checkmark))

        row.append(row_end)

        if not opts.skip_empty or not empty:
            yield ''.join(row)

    yield table_end

    yield html_footer % {'utc': utc}
