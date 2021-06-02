# mirrorbrain

This code used to live in Subversion so far, hosted on mirrorbrain.org. Maybe it's time to experiment with moving to GitHub.
*poeml, Mon 11 May 2015 23:06:47 CEST*

# What is it?

MirrorBrain is an open source framework to run a content delivery network using mirror servers. It solves a challenge that many popular open source projects face - a flood of download requests, often magnitudes more than any single site could practically handle.

The central (and probably the most obvious) part is a "download redirector" which automatically redirects requests from web browsers or download programs to a mirror server near them. 

One example of a running instance is http://download.opensuse.org/

# Features

For clients (users):

* allows to have one central URL for clients to download content
* uses geolocation and global routing data to find the closest mirror for clients
* automatically keeps cryptohashes of all files and can serve these on request
* optionally generates Metalinks ([RFC5854](https://tools.ietf.org/html/rfc5854)) and Torrents in realtime
* provides automatically generated mirror list for overview (For example: https://mirrors.opensuse.org/)
* Allows to list available mirrors for single files to choose ([example](http://download.opensuse.org/distribution/openSUSE-current/repo/oss/README.mirrorlist))

For mirrors: 

* load-balancing of mirrors, based on weighting
* ability to limit requests for a mirror to its own network or country
* file level granularity (mirrors don't have to mirror the full file tree - they can choose what they want)
* reliably assess large file support of mirrors

For admins of a MirrorBrain server:

* has proven to handle hundreds of requests per second
* ability to NOT redirect certain requests, for security reasons
* content on mirrors can be protected by URL signing (clients can only download from mirrors if they successfully authenticated with the MirrorBrain server)
* commandline tools and Python module for maintenance tasks
* support for running behind a load balancer, using e.g. X-Forwarded-for header for the clients IP address
* integrated in Apaches module API, for compatibility with numerous other existing Apache modules, e.g. SSL
* multiple instances are supported to run in one Apache (one per virtual host)
* flexible logging
* more than a redirector
  * serve automatically generated cryptohashes (MD5, SHA1, SHA256)
  * generation of [RFC5854](https://tools.ietf.org/html/rfc5854) Metalinks
  * support for [RFC3230](https://tools.ietf.org/html/rfc3230) - Instance Digests in HTTP
  * support for [RFC6249](https://tools.ietf.org/html/rfc6249) - Metalink/HTTP: Mirrors and Hashes
  * generation of Torrents (including the closest mirrors as seeds) 
  * support for zsync
  * support for native Yum mirror lists, compatible to Fedora and CentOS
