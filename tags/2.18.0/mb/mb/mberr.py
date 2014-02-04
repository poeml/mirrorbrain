#!/usr/bin/python

# Copyright (C) 2008-2010 Peter Poeml.  All rights reserved.
# This program is free software; it may be used, copied, modified
# and distributed under the terms of the GNU General Public Licence,
# either version 2, or (at your option) any later version.


# exception classes

class MbBaseError(Exception):
    def __init__(self, args=()):
        Exception.__init__(self)
        self.args = args
    def __str__(self):
        return ''.join(self.args)

class SignalInterrupt(Exception):
    """Exception raised on SIGTERM and SIGHUP."""

class UserAbort(MbBaseError):
    """Exception raised when the user requested abortion"""

class NoConfigfile(MbBaseError):
    """Exception raised when mb's configfile cannot be found"""
    def __init__(self, fname, msg):
        MbBaseError.__init__(self)
        self.file = fname
        self.msg = msg

class ConfigError(MbBaseError):
    """Exception raised when there is an error in the config file"""
    def __init__(self, msg, fname):
        MbBaseError.__init__(self)
        self.msg = msg
        self.file = fname

class MirrorNotFoundError(MbBaseError):
    """Raised when a mirror wasn't found in the database"""
    def __init__(self, identifier):
        MbBaseError.__init__(self)
        self.identifier = identifier
        self.msg = 'A mirror with identifier %r does not exist in the database' \
                % self.identifier

class SocketError(MbBaseError):
    """Raised for network errors"""
    def __init__(self, url, msg):
        MbBaseError.__init__(self)
        self.url = url
        self.msg = 'Could not access %r: %r' % (url, msg)

class NameOrServiceNotKnown(MbBaseError):
    """Raised when a hostname could not be looked up in the DNS"""
    def __init__(self, hostname):
        MbBaseError.__init__(self)
        self.msg = 'DNS lookup for hostname %r failed: Name or service not known' % hostname

