Prerequirements for mirrors
===========================


* must run rsync, FTP or HTTP for scanning. rsync is best.


Creating and maintaining the mirror database
============================================


Creating a new mirror
---------------------

Run the :program:`mb new` command, like this::

  mb new opensuse.uib.no -H 'http://opensuse.uib.no/' -F 'ftp://opensuse.uib.no/pub/Linux/Distributions/opensuse/' -R rsync://opensuse.uib.no/opensuse-full/

This creates a new entry in the mirror database for this mirror.


Listing mirrors
---------------

mb list


Displaying details about a mirror
---------------------------------

mb show

Changing a mirror
---------------------

mb edit
