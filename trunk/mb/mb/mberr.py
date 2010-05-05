#!/usr/bin/python

# Copyright (C) 2008 Peter Poeml.  All rights reserved.
# This program is free software; it may be used, copied, modified
# and distributed under the terms of the GNU General Public Licence,
# either version 2, or (at your option) any later version.


# exception classes

class Error(Exception):
    """Base class for MirrorBrain exceptions."""

    def __init__(self, msg=''):
        self.message = msg
        Exception.__init__(self, msg)

    def __repr__(self):
        return self.message

    __str__ = __repr__


class CommandFailedError(Error):
    """Raised when an executed command returns != 0."""

    def __init__(self, retcode, stderr, cmd):
        Error.__init__(self, 'Command failed. It returned: %r' % (retcode,))
        self.retcode = retcode
        self.stderr  = stderr
        self.cmd     = cmd

class CommandExecuteError(Error):
    """Raised when a command could not be executed"""

    def __init__(self, cmd, msg):
        Error.__init__(self, 'Could not execute command %r: %r' % (cmd, msg,))
        self.cmd = cmd
        self.msg = msg

class MirrorNotFoundError(Error):
    """Raised when a mirror wasn't found in the database"""

    def __init__(self, msg):
        Error.__init__(self, 'A mirror with identifier %r doesn\'t exist in the database' % (msg,))
        self.msg = msg

class SocketError(Error):
    """Raised for network errors"""

    def __init__(self, url, msg):
        Error.__init__(self, 'Cannot access %r: %r' % (url, msg,))
        self.url = url
        self.msg = msg

class NameOrServiceNotKnown(Error):
    """Raised when a hostname could not be looked up in the DNS"""

    def __init__(self, msg):
        Error.__init__(self, 'DNS lookup for hostname %r failed: Name or service not known' % (msg,))
        self.msg = msg


