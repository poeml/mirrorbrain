			jw, Fri Jan 19 17:14:08 CET 2007

scanner.pl is implemented as an ftp-crawler.
It correctly adds, updates, and removes entries in the file_server table.
It honors the enabled flags in the server table and ignores the status flags.


TODO: 
  - add some index hashes to the database. Currently it does a linear search.
  - implement an rsync log parser. 
    This gives fastest updates for push mirrors, when triggered after rsync.
  - implement a post rsync trigger hook for push mirrors, to do the above.
  - implement a pre rsync trigger hook for push mirrors, to disable them 
    while they are being updated.
  - add geoip DB
  - implement a test client that honors the status fields.
