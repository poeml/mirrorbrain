#!/usr/bin/env python

from distutils.core import setup

setup(name='mirrordoctor',
      version='1.0',
      description='MirrorDoctor, a tool to maintain the MirrorBrain database',
      author='Peter Poeml',
      author_email='poeml@suse.de',
      license='GPL',
      url='http://mirrorbrain.org/',

      packages=['mb'],
      scripts=['mirrordoctor.py'],
     )
