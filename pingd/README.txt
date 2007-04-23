Example for the perl pingd:
===========================
1) set Database config (../config/config.pl)
2) add to crontab:
0,30 * * * *    root /usr/bin/perl /suse/mpolster/redirector/download-redirector-v2/pingd/ping.pl 


Example for the python pingd:
=============================
1) use the example pingdrc and copy it to /root/.pingdrc
2) add to crontab:
*/5 * * * *     root /usr/local/bin/pingd.py &>/dev/null

usage: pingd.py [options] [<mirror_identifier>+]

options:
  -h, --help            show this help message and exit
  -l LOGFILE, --log=LOGFILE
                        path to logfile
  -L LOGLEVEL, --loglevel=LOGLEVEL
                        Loglevel (DEBUG, INFO, WARNING, ERROR, CRITICAL)
  -T EMAIL, --mailto=EMAIL
                        email adress to mail warnings to
  -t TIMEOUT, --timeout=TIMEOUT
                        Timeout in seconds
  -n, --no-run          don't update the database. Only look
  -e, --enable-revived  enable revived servers

