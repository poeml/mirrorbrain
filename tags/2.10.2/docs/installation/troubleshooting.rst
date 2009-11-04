
Troubleshooting
---------------

If Apache doesn't start, or anything else seems wrong, make sure to check
Apache's error_log. It usually points into the right direction.

A general note about Apache configuration which might be in order. With most
config directives, it is important to pay attention where to put them - the
order does not matter, but the context does. There is the concept of directory
contexts and vhost contexts, which must not be overlooked.  Things can be
"global", or inside a <VirtualHost> container, or within a <Directory>
container.

This matters because Apache applies the config recursively onto subdirectories,
and for each request it does a "merge" of possibly overlapping directives.
Settings in vhost context are merged only when the server forks, while settings
in directory context are merged for each request. This is also the reason why
some of mod_asn's config directives are programmed to be used in one or the
other context, for performance reasons.

The install docs you are reading attempt to always point out in which context
the directives belong.

.. note:: To get help, please subscribe to the mirrorbrain mailing list, see
          http://mirrorbrain.org/communication .  Questions can be answered
          there, and all kind of feedback is appreciated.

