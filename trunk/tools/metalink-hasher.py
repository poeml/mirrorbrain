#!/usr/bin/python

# metalink-hasher -- create metalink hashes
# Copyright 2008,2009,2010 Peter Poeml

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



# This script was superceded with functionality in the 'mb' tool, 
# in the beginning of 2010.

import sys
import os

args = ['mb', 'makehashes'] + sys.argv[2:]
os.execlp('mb', *args)

sys.exit(0)
