#!/usr/bin/env python

from distutils.core import setup, Extension

s = setup(name='mb',
      version='2.13.0',
      description='mb, a tool to maintain the MirrorBrain database',
      author='MirrorBrain project',
      author_email='info@mirrorbrain.org',
      license='GPLv2',
      url='http://mirrorbrain.org/',

      packages=['mb'],
      scripts=['mb.py'],

      ext_modules=[Extension('zsync', sources=['zsyncmodule.c'])],
     )


# Since the "mb" script has the same name as the Python module, it'd
# try to import itself as long as there's mb.py in the script's directory.
# Therefore, we remove mb.py and only leave mb.
# Don't know if there's a better way to achieve this...
from distutils.command.install_scripts import install_scripts
script_install_dir = s.get_command_obj('install_scripts').install_dir

from os.path import join
from os import unlink
unlink(join(script_install_dir, 'mb.py'))
