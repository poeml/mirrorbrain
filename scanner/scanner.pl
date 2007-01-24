#!/usr/bin/perl -w

################################################################################
# scanner.pl daemon for working through opensuse directories.
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

#
# 2007-01-19, jw: - added md5 support. Speedup of ca. factor 2. 
#                   Requires column 'path_md5 binary(22)' in file_server;
#                   Obsoletes 'file.id' and 'file_server.fileid';
#                 - Usage: optional parameter serverid, to limit the crawler.
# 

use DBI;
use Date::Parse;
use strict;
use LWP::UserAgent;
use Data::Dumper;
use Digest::MD5;
use Time::HiRes;

$ENV{FTP_PASSIVE} = 1;

# Create a user agent object
my $ua = LWP::UserAgent->new;
$ua->agent("scanner/0.1");

my $verbose = 1;
my $use_md5 = 1;

my $only_server_id = shift;

my $dbh = DBI->connect( 'dbi:mysql:redirector', 'root', '',
     { PrintError => 0 } ) or die $DBI::errstr;

my $sql = qq{SELECT * FROM server};
my $ary_ref = $dbh->selectall_arrayref( $sql )
		   or die $dbh->errstr();

for my $row (@$ary_ref)
{
  my($id, $identifier, $baseurl, $baseurl_ftp, $enable, 
      $status_url, $status_ftp, $status_ping) = @$row;

  next if defined($only_server_id) and $id != $only_server_id;

  if ($enable == 1)
  {
    print "$id: $baseurl_ftp : \n" if $verbose;

    my $start = time();
    my @dirlist = ftp_readdir($id, $baseurl_ftp, '');
    my $duration = time() - $start;
    $duration = 1 if $duration < 1;
    my $fps = int(scalar(@dirlist)/$duration);

    my $sql = "DELETE FROM file_server WHERE serverid = $id 
    	       AND timestamp_scanner <= (SELECT last_scan FROM server 
	       WHERE serverid = $id limit 1);";

    my $sth = $dbh->prepare( $sql );
              $sth->execute() or die $sth->errstr;

    $sql = "UPDATE server SET last_scan = CURRENT_TIMESTAMP, scan_fps = $fps WHERE id = $id;";
    $sth = $dbh->prepare( $sql );
           $sth->execute() or die $sth->err;

    print Dumper $id, \@dirlist if $verbose > 1;
  }
}

$dbh->disconnect();
exit;


sub ftp_readdir
{
  my ($id, $url, $name) = @_;

  print "$id $url/$name\n" if $verbose;
  my $content = cont("$url/$name");
  
  if ($content =~ m/^\d\d\d\s/)
  {
    print $content if $verbose > 2;
    return;
  }  

  print $content."\n" if $verbose > 2;

  my @text = split( "\n", $content );

  my @r;
  for my $i (0..$#text)
  {
    if ($text[$i] =~ m/^([d-]).*(\w\w\w\s+\d\d?\s+\d\d:?\d\d)\s+([\S]+)$/)
    {
      next if $3 eq "." or $3 eq "..";
      my $timestamp = $2;

      #convert to timestamp
      my $time = str2time($timestamp);
	
      my $t = length($name) ? "$name/$3" : $3;

      if ($1 eq "d")
      {
        push @r, ftp_readdir($id, $url, $t);
      }
      else
      {
        #save timestamp and file in database
	my $key = $use_md5 ? 'path_md5' : 'fileid';

        my $fileid = getfileid($t);
	if (checkfileserver($id, $fileid))
	{
	my $sql = "UPDATE file_server SET 
		   timestamp_file = FROM_UNIXTIME(?),
		   timestamp_scanner = CURRENT_TIMESTAMP()
		   WHERE $key = ? AND serverid = ?;";

	my $sth = $dbh->prepare( $sql );
	              $sth->execute( $time, $fileid, $id ) or die $sth->err
	}  
	else
	{

        my $sql = "INSERT INTO file_server SET $key = ?,
		   serverid = ?,
	           timestamp_file = FROM_UNIXTIME(?), 
		   timestamp_scanner = CURRENT_TIMESTAMP();"; 
		   #convert timestamp to mysql timestamp

        my $sth = $dbh->prepare( $sql );
		      $sth->execute( $fileid, $id, $time ) or die $sth->err;
        }

        push @r, [ $t , $time ];
      }
    }
  }
  return @r;
}

sub cont 
{
  my $url = shift;

  # Create a request
  my $req = HTTP::Request->new(GET => $url);

  # Pass request to the user agent and get a response back
  my $res = $ua->request($req);

  # Check the outcome of the response
  if ($res->is_success) 
  {
    return ($res->content);
  }
  else 
  {
    return ($res->status_line);
  }        
}


# getfileid can operate in two modes:
# with use_md5, it only makes sure that the path is in table file.
# and returns an md5sum, ignoring the id.
# 
# else it has to bother to retrieve the id after it did an insert.
#
# we always update table file, so that we can ask 
# the database to enumerate the files we have seen.
#
sub getfileid
{
  my $path = shift;

  my $sql = "SELECT id FROM file WHERE path = " . $dbh->quote($path);

  my $ary_ref = $dbh->selectall_arrayref( $sql )
                     or die $dbh->errstr();
  my $id = $ary_ref->[0][0];
  return $id if defined $id;

  return ($use_md5 ? Digest::MD5::md5_base64($path) : $id) if defined $id;
  
  $sql = "INSERT INTO file SET path = ?;";

  my $sth = $dbh->prepare( $sql );
                $sth->execute( $path ) or die $sth->err;

  return Digest::MD5::md5_base64($path) if $use_md5;

  $sql = "SELECT id FROM file WHERE path = " . $dbh->quote($path);

  $ary_ref = $dbh->selectall_arrayref( $sql )
                       or die $dbh->errstr();

  $id = $ary_ref->[0][0];

  return $id;
}

sub checkfileserver
{
  my ($serverid, $fileid) = @_;

  my $sql = $use_md5 ?
    "SELECT 1 FROM file_server WHERE path_md5 = '$fileid' AND serverid = $serverid;" :
    "SELECT 1 FROM file_server WHERE fileid = $fileid AND serverid = $serverid;";

  my $ary_ref = $dbh->selectall_arrayref( $sql )
                       or die $dbh->errstr();

  if (defined $ary_ref->[0])
  {
    return 1
  } 
  else
  {
    return 0;
  }
}  
