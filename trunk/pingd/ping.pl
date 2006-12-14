#!/usr/bin/perl -w

################################################################################
# ping.pl daemon for probing the opensuse mirror server.
# Copyright (C) 2006 Martin Polster, Novell Inc.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
################################################################################

use Net::Ping;
use DBI;
use strict;

my $verbose = 0;

my $dbh = DBI->connect( 'dbi:mysql:redirector', 'root', '',
    { PrintError => 0 } ) or die $DBI::errstr;

my $sql = qq{SELECT * FROM servers};
my $ary_ref  = $dbh->selectall_arrayref( $sql )
		   or die $dbh->errstr();

for my $row (@$ary_ref)
{
  my($id, $identifier, $baseurl, $baseurl_ftp, $status) = @$row;
  my $res = ping($identifier);

  print "$identifier : $res \n" if $verbose;

  if ($res ne $status)
  {
    my $sql = "UPDATE servers SET status = ? WHERE ( id = ?);";
    my $sth = $dbh->prepare( $sql );
       $sth->execute( $res, $id );
  }
}

$dbh->disconnect();
exit 0;

sub ping 
{
  my $host = shift;
  my $status = "offline";

  my $p = Net::Ping->new();
  $status = "online" if $p->ping($host);
  $p->close();

  return $status;
}
