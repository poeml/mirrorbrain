This is a redirector module for apache.

It requires a backend to work with, which can be found at
https://forgesvn1.novell.com/svn/opensuse/trunk/tools/download-redirector-v2

RPM packages are here:
http://software.opensuse.org/download/Apache:/Modules/

It requires libGeoIP, libapr_memcache, and mod_form.
It can be built manually with
apxs2 -c -lGeoIP -lapr_memcache -Wc,"-Wall -g" mod_zrkadlo.c


The module understands query arguments (optionally appended to the URL) for 
diagnostic or other uses. See the config example for details.


Q: What does "zrkadlo" mean?
A: 'zrkadlo' is slovakian for 'mirror'.



This should give a picture how the score values behave:

 % ./rand.py 100000 100 100 100
score:   100 count: 33279 (33%)
score:   100 count: 33378 (33%)
score:   100 count: 33343 (33%)
 % ./rand.py 100000 100 50 50 
score:   100 count: 58148 (58%)
score:    50 count: 20893 (20%)
score:    50 count: 20959 (20%)
 % ./rand.py 100000 100 200 10 
score:   100 count: 24359 (24%)
score:   200 count: 73588 (73%)
score:    10 count:  2053 (2%)

 % ./randint 100000 100 100 100
score:   100 count: 33474 (33.47%)
score:   100 count: 33118 (33.12%)
score:   100 count: 33408 (33.41%)
                      (100.00%)
 % ./randint 100000 100 50 50  
score:   100 count: 58301 (58.30%)
score:    50 count: 20840 (20.84%)
score:    50 count: 20859 (20.86%)
                      (100.00%)
 % ./randint 100000 100 200 10
score:   100 count: 24620 (24.62%)
score:   200 count: 73337 (73.34%)
score:    10 count:  2043 (2.04%)
                      (100.00%)


