#!/usr/bin/env python3

from setuptools import setup, Extension
import sys

import mb.appinfo

requirements = [i.strip() for i in open("requirements.txt").readlines()]

s = setup(
    name         = 'mb',
    version      = mb.appinfo.version,
    description  = 'mb, a tool to maintain the MirrorBrain database',
    author       = mb.appinfo.author_name,
    author_email = mb.appinfo.author_email,
    license      = mb.appinfo.license,
    url          = mb.appinfo.url,

    packages     = ['mb'],
    scripts      = ['scripts/mb', 'scripts/mirrorprobe'],
    # We enforce python 3.5 here as that matches the list
    # of the geoip2 dependency
    python_requires=">=3.5",
    install_requires=requirements,

    ext_modules=[Extension('zsync', sources=['zsyncmodule.c'])],
    classifiers=[
        "Development Status :: 5 - Production/Stable",
        "Environment :: Web Environment",
        "Intended Audience :: Developers",
        "Intended Audience :: System Administrators",
        "License :: OSI Approved :: GPL-2.0",
        "Programming Language :: Python :: 3.5",
        "Programming Language :: Python :: 3.6",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python",
        "Topic :: Internet :: Proxy Servers",
        "Topic :: Internet",
    ],
)
