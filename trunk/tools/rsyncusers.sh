#!/bin/bash

# analyze rsync logfile to list rsync users per rsync module

# Copyright 2008,2009,2010,2011 Peter Poeml <poeml@cmdline.net>
# 
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License version 2
# as published by the Free Software Foundation;
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA

__version__='1.0'
__author__='Peter Poeml <poeml@cmdline.net>'
__copyright__='Peter poeml <poeml@cmdline.net>'
__license__='GPLv2'
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

