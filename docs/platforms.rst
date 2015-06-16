.. _platforms:


Platforms
=========

This table is work in progress (started in 2014), in an attempt to collect an
overview of platform features.

===================================  ========  ==========  ====  ==========  ======  =======
           Platform                  Apache    PostgreSQL  ip4r   packaged   tested  remarks
===================================  ========  ==========  ====  ==========  ======  =======
Debian 9.0 (next)                                                  
Debian 8.0 (Jessie)                  2.4.10    9.4         2.0.2  obs
Debian 7.0 (Wheezy)                  2.2.22    9.1         1.05   obs        yes     stephan48
Debian 6.0 (Squeeze)                 2.2.16    8.4         1.04                      mod_geoip only 1.2.5, GeoIP too old for current mod_geoip
Debian 5.0 (Lenny)                                                 
-----------------------------------  --------  ----------  ----  ----------  ------  -------
-----------------------------------  --------  ----------  ----  ----------  ------  -------
Ubuntu 13.10 (Saucy Salamander)      2.4.6     9.1         2.0     
Ubuntu 13.04 (Raring Ringtail)       2.2.22    9.1         1.05    
Ubuntu 12.10 (Quantal Quetzal)       2.2.22    9.1         1.05                      sqlobject upstream bug was fixed in this release (#120)
Ubuntu 12.04 LTS (Precise Pangolin)  2.2.22    9.1         1.05   obs        2.28.1  floeff; mod_geoip only 1.2.5, but packaged in obs
Ubuntu 11.10 (Oneiric Ocelot)                                                2.17.0  floeff
Ubuntu 10.04 LTS (Lucid Lynx)        2.2.14    8.4         1.04                      mod_geoip too old, GeoIP too old for current mod_geoip
-----------------------------------  --------  ----------  ----  ----------  ------  -------
-----------------------------------  --------  ----------  ----  ----------  ------  -------
openSUSE 13.2                                                                        
openSUSE 13.1                        2.4.6     9.2         1.05                      
openSUSE 12.3                        2.2.22    9.2         1.05    
openSUSE 12.2                        2.2.22    9.1         1.05    
openSUSE 12.1                        2.2.21    9.1         1.05  no longer   2.17.0
SUSE SLE 11                          2.2.10                1.05    
SUSE SLE 10                          2.2.0(?)                      
-----------------------------------  --------  ----------  ----  ----------  ------  -------
-----------------------------------  --------  ----------  ----  ----------  ------  -------
Rawhide                              2.4.7     9.3         2.0
Fedora 20                            2.4.6     9.3         1.05    
Fedora 19                            2.4.6     9.2         1.05    
Fedora 18                            2.4.6     9.2         1.05    
-----------------------------------  --------  ----------  ----  ----------  ------  -------
-----------------------------------  --------  ----------  ----  ----------  ------  -------
RHEL 6                                                                       no      mirrorbrain package builds in OBS
RHEL 5                                                             
RHEL 4                                                             
-----------------------------------  --------  ----------  ----  ----------  ------  -------
-----------------------------------  --------  ----------  ----  ----------  ------  -------
CentOS 6                                                           
CentOS 5                                                           
===================================  ========  ==========  ====  ==========  ======  =======


Suggestions for other features to track:

* mod_geoip with IPv6 capability
* see also http://mirrorbrain.org/issues/issue16
* very old mod_geoip versions *1.1.8* didn't return continent lookup data

Ubuntu mod_geoip versions: http://packages.ubuntu.com/search?keywords=mod-geoip&searchon=names 
Debian mod_geoipip versions http://packages.debian.org/search?suite=all&searchon=names&keywords=libapache2-mod-geoip

Package search URLs:

* https://packages.debian.org/search
* http://packages.ubuntu.com/ and https://launchpad.net/
