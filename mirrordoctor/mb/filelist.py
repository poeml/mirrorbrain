


def get_filelist(url):

    if url.startswith('rsync'):
        import mb.crawlers.rsync
        return mb.crawlers.rsync.get_filelist(url)

    elif url.startswith('http'):
        import mb.crawlers.http
        return mb.crawlers.http.get_filelist(url)

    elif url.startswith('ftp'):
        import mb.crawlers.ftp
        return mb.crawlers.ftp.get_filelist(url)

    else:
        import sys
        sys.exit('unknown error... url is \'%s\'' % url)


