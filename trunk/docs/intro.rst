Introduction
============

This documentation describes how to install, configure and use MirrorBrain.

It may be useful to consult the :ref:`release_notes` for changes.

The detailed history of changes in the documentation itself could be looked at
`here <http://svn.mirrorbrain.org/viewvc/mirrorbrain/trunk/docs/>`_.


How to improve this documentation
---------------------------------

Working on the documentation is easy. This section explains how it is done.


Sources
^^^^^^^

The documentation sources are maintained in the `docs subdirectory`_ of the
MirrorBrain Subversion repository. The source is formatted in
`reStructuredText`_. reStructuredText is simple to learn, and resembling Wiki
markup which is quick to edit. Every page of the online documentation has a
link named "Source" which displays the corresponding source file in the
subversion repository. You can use this to look at the source and get a feeling
for the way the format works.

HTML is generated using the `Sphinx Python Documentation Generator`_. Every
change in the svn repository directly becomes visible at
http://mirrorbrain.org/docs/, through a post-commit hook running the generator.

Check out a working copy of the source with this command::

    svn checkout http://svn.mirrorbrain.org/viewvc/mirrorbrain/trunk/ mirrorbrain

The `reStructuredText Primer`_ is a helpful resource.


Submitting changes
^^^^^^^^^^^^^^^^^^

To submit changes, there are several options:

* sending patches to the mirrorbrain@mirrorbrain.org mailing list
* requesting write access to the subversion repository (do so by writing to the
  mailing list)
* of course, it is fully appropriate (and appreciated) if you simply send plain
  mail, pointing out deficiencies, or giving suggestions.


Testing documentation locally before committing
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

It is useful to test changes by generating HTML locally before committing. To
be able to do this, you need to install the `Sphinx Python Documentation
Generator`_. It is available readily packaged on most platforms, named
``python-sphinx``, ``py25-sphinx`` or similarly.

Generating the documentation is as easy as::

    cd docs
    make html

Then just open :file:`_build/html/index.html` in your web browser. Simply rerun
the :command:`make` command after changes, watch its output for errors or
warnings, and reload your browser window.


.. _`docs subdirectory`: http://svn.mirrorbrain.org/viewvc/mirrorbrain/trunk/docs/
.. _`reStructuredText`: http://docutils.sf.net/rst.html
.. _`Sphinx Python Documentation Generator`: http://sphinx.pocoo.org/
.. _`reStructuredText Primer`: http://sphinx.pocoo.org/rest.html

