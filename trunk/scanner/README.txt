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
  - implement an rsync log parser. 
    This gives fastest updates for push mirrors, when triggered after rsync.
  - implement a post rsync trigger hook for push mirrors, to do the above.
  - implement a pre rsync trigger hook for push mirrors, to disable them 
    while they are being updated.
  - add geoip DB
