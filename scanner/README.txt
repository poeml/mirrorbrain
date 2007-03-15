			jw, Fri Jan 19 17:14:08 CET 2007

scanner.pl is implemented as an ftp-crawler.
It correctly adds, updates, and removes entries in the file_server table.
It honors the enabled and status flags in the server table.

../test/torture.pl is a client that can test simple redirects or
run random access redirects at maximum speed. 
Galerkin now can serve ca 1000 redirects per second 
(indices have been added to the database, md5 hashes added.
 without md5 hashes, Galerkin serves ca 600 redirects per seconds).


TODO: 
  - add to -N option:
    -N baseurl -s score -c country -r region(continent)
    -N baseurl_ftp 
    -N baseurl_rsync

  - ~/config.pl  or -c configfile
    config: exclude list
    .xml
    .xml.gz
    .asc
    .repo
    /repoview/*

  - option -A serverid path | [url]
  - option -D serverid path | [url]

  - loop mode with timeout after last scan.

  - slowscan: default 2 sec pause after each directory

  - implement a post rsync trigger hook for push mirrors, to do the above.
  - implement a pre rsync trigger hook for push mirrors, to disable them 
    while they are being updated.
  - add geoip DB

  - implement an rsync log parser. 
    This gives fastest updates for push mirrors, when triggered after rsync.

  - http head request to check files > 2 GB
