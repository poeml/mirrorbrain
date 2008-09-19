#!/bin/bash

# analyze rsync logfile to list rsync users per rsync module

__version__='1.0'
__author__='Peter Poeml <poeml@suse.de>'
__copyright__='Peter poeml <poeml@suse.de>'
__license__='GPL'
__url__='http://mirrorbrain.org'


LOGFILE=${1:?An rsync logfile is needed as argument. (It may be compressed.)}

case $LOGFILE in
    *.bz2) CAT=bzcat; GREP=zgrep;;
    *.gz)  CAT=zcat; GREP=zgrep;;
    *)     CAT=cat; GREP=grep;;
esac

$CAT $LOGFILE | \
for i in $(grep "rsync on " | awk '{ print $6 }' | cut -d/ -f1 | sort | uniq); do
        echo
        echo $i
        $GREP "rsync on $i" $LOGFILE \
          | awk '{ printf "  %-18s %s\n", $9, $8 }' \
          | sed 's/(//g ; s/)//g' \
          | sort -n \
          | uniq
done

