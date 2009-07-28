Introduction
============

MirrorBrain is an open source framework to run a content delivery network using
mirror servers. It solves a challenge that many popular open source projects
face - a flood of download requests, often magnitudes more than a single site
could practically handle.

A central (and probably the most obvious) part is a "download redirector" which
automatically redirects requests from web browsers or download programs to a
mirror server near them.

Choosing a suitable mirror for a users request is the key, and MirrorBrain uses
geolocation and global routing data to make a sensible choice, and achieve
load-balancing for the mirrors at the same time. The used algorithm is both
sophisticated and easy to control and tune. In addition, MirrorBrain monitors
mirrors, scans them for files, generates mirror lists, and more.


