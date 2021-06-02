import time

postgresql_header = """
--
-- generated on %s
--
""" % time.asctime()

postgresql_template = """\
--
-- %(identifier)s
--
INSERT INTO server (
  identifier, baseurl, baseurl_ftp, baseurl_rsync, enabled, status_baseurl,
  region, country, asn, prefix,
  score, comment, operator_name, operator_url, public_notes,
  admin, admin_email, lat, lng,
  country_only, region_only, as_only, prefix_only,
  other_countries, file_maxsize, scan_fpm)
VALUES (
  '%(identifier)s', '%(baseurl)s', '%(baseurlFtp)s', '%(baseurlRsync)s', '%(enabled)s', '%(statusBaseurl)s',
  '%(region)s', '%(country)s', '%(asn)s', '%(prefix)s',
  '%(score)s', $QUOTE$%(comment)s$QUOTE$, $QUOTE$%(operatorName)s$QUOTE$, '%(operatorUrl)s', $QUOTE$%(publicNotes)s$QUOTE$,
  $QUOTE$%(admin)s$QUOTE$, $QUOTE$%(adminEmail)s$QUOTE$, '%(lat)s', '%(lng)s',
  '%(countryOnly)s', '%(regionOnly)s', '%(asOnly)s', '%(prefixOnly)s',
  '%(otherCountries)s', '%(fileMaxsize)s', '%(scanFpm)s');
"""

django_header = """#!/usr/bin/env python3
import os, sys

mybasepath = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, mybasepath)
os.environ['DJANGO_SETTINGS_MODULE'] = 'mirrordjango.settings'

from django.db import connection

from mirrordjango.mb.models import Contact, Operator, Project, Server, Mirror

"""

django_template = """\

# ------------------------------------------------------------
try:
    c = Contact.objects.get_or_create(username=%(admin)r, password='UNSET', name=%(admin)r, email=%(adminEmail)r)[0]
except:
    connection.connection.rollback()
    c = None
o = Operator.objects.get_or_create(name='%(identifier)s', logo='')[0]
p = Project.objects.filter(name='%(project)s')
p = p[0]
s = Server.objects.get_or_create(identifier='%(identifier)s', operator=o, region='%(region)s', country='%(country)s', country_only='%(countryOnly)s', region_only='%(regionOnly)s', as_only='%(asOnly)s', prefix_only='%(prefixOnly)s', other_countries=%(otherCountries)r, file_maxsize='%(fileMaxsize)s', comment=%(comment)r, bandwidth=1)[0]
m = Mirror.objects.get_or_create(http='%(baseurl)s', ftp='%(baseurlFtp)s', rsync='%(baseurlRsync)s', prio='%(score)s', project=p, server=s)[0]
# s.mirrors.add(m)
if c:
    s.contacts.add(c)

"""

# mirmon configured with "list_style = apache"
mirmon_apache_template = """%(proto)s\t%(country)s\t%(url)s\t%(adminEmail)s"""
# mirmon configured with "list_style = plain"
mirmon_template = """%(country)s\t%(url)s\t%(adminEmail)s"""
