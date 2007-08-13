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

- Timeout 20min -> fallback to next protocol
  (readdir_rsync to ftp5.gwdg.de apparently hangs sometimes)


DONE  - add to -N option:
DONE    -N baseurl score=score country=country region=continent enable=1 name=identifier
DONE    -N ftp=baseurl_ftp 
DONE    -N rsync=baseurl_rsync
DONE
DONE  - option -A serverid path | [url]

  - option -D serverid path | [url]


DONE  - ~/config.pl  or -c configfile
DONE    config: exclude list
DONE    .xml
DONE    .xml.gz
DONE    .asc
DONE    .repo
DONE    /repoview/*
DONE    /drpmsync/*
DONE (not as config file, but as builtin list with command line option -i)

  - loop mode with timeout after last scan.
    -> parallel_test.pl

DONE  - slowscan: default 2 sec pause after each directory

  - implement a post rsync trigger hook for push mirrors, to do the above.
  - implement a pre rsync trigger hook for push mirrors, to disable them 
    while they are being updated.
  - add geoip DB

  - implement an rsync log parser. 
    This gives fastest updates for push mirrors, when triggered after rsync.

  - http head request to check files > 2 GB

  - find http 403 error from darix/deckel.

  - port to postgresql, so that mysql or postgres can be selected by config option.
