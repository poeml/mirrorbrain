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
# 2007-01-24, jw  - Multiple server ids as parameter accepted. 
#                   recording scan_fpm for benchmarking, 
#                   and inserting both, fileid and path_md5 in file_server.
#                   http_readdir added.
# 2007-02-15, jw  - rsync_readdir added.
# 		    
# FIXME: 
# should do optimize table file, file_server;
# once in a while.
#
# 
#######################################################################
# rsync protocol
#######################################################################
#
# Copyright (c) 2005 Michael Schroeder (mls@suse.de)
#
# This program is licensed under the BSD license, read LICENSE.BSD
# for further information
#
#######################################################################

use strict;

use DBI;
use Date::Parse;
use LWP::UserAgent;
use Data::Dumper;
use Digest::MD5;
use Time::HiRes;
use Socket;
use bytes;

$SIG{'PIPE'} = 'IGNORE';
my $rsync_muxbuf = '';

$SIG{__DIE__} = sub 
{
  my @a = ((caller 3)[1..3], '=>', (caller 2)[1..3], '=>', (caller 1)[1..3]);
  print "__DIE__: (@a)\n";
  die @_;
};

$ENV{FTP_PASSIVE} = 1;

# Create a user agent object
my $ua = LWP::UserAgent->new;
$ua->agent("openSUSE Scanner/0.2 (See http://en.opensuse.org/Mirrors/Scanner)");

my $verbose = 1;
my $use_md5 = 1;

my %only_server_ids = map { $_ => 1 } @ARGV;

my $dbh = DBI->connect( 'dbi:mysql:dbname=redirector;host=galerkin.suse.de', 'root', '',
     { PrintError => 0 } ) or die $DBI::errstr;

my $sql = qq{SELECT * FROM server};
my $ary_ref = $dbh->selectall_hashref($sql, 'id')
		   or die $dbh->errstr();

for my $row (sort { $a->{id} <=> $b->{id} } values %$ary_ref)
{
  next if keys %only_server_ids and !defined $only_server_ids{$row->{id}};

  if ($row->{enabled} == 1)
  {
    print "$row->{id}: $row->{identifier} : \n" if $verbose;

    my $start = time();
    my @dirlist = rsync_readdir($row->{id}, $row->{baseurl_rsync}, '');
    @dirlist    =   ftp_readdir($row->{id}, $row->{baseurl_ftp}, '') if !@dirlist and $row->{baseurl_ftp};
    @dirlist    =  http_readdir($row->{id}, $row->{baseurl}, '')     if !@dirlist and $row->{baseurl};

    my $duration = time() - $start;
    $duration = 1 if $duration < 1;
    my $fpm = int(60*scalar(@dirlist)/$duration);

    my $sql = "DELETE FROM file_server WHERE serverid = $row->{id} 
    	       AND timestamp_scanner <= (SELECT last_scan FROM server 
	       WHERE serverid = $row->{id} limit 1);";

    my $sth = $dbh->prepare( $sql );
              $sth->execute() or die $sth->errstr;

    $sql = "UPDATE server SET last_scan = CURRENT_TIMESTAMP, scan_fpm = $fpm WHERE id = $row->{id};";
    $sth = $dbh->prepare( $sql );
           $sth->execute() or die $sth->err;

    print Dumper $row->{id}, \@dirlist if $verbose > 1;
  }
}

$dbh->disconnect();
exit;

sub http_readdir
{
  my ($id, $url, $name) = @_;
  $url =~ s{/+$}{};	# we add our own trailing slashes...
  $name =~ s{/+$}{};

  my @r;
  print "$id $url/$name\n" if $verbose;
  my $contents = cont("$url/$name");
  if ($contents =~ s{^.*<pre>.*<a href="\?C=.;O=.">}{}s)
    {
      ## good, we know that. it is a standard apache dir-listing.
      ## 
      ## bad, apache shows symlinks as a copy of the file or dir they point to.
      ## no way to avoid duplicate crawls.
      ##
      $contents =~ s{</pre>.*$}{}s;
      for my $line (split "\n", $contents)
        {
	  if ($line =~ m{^(.*)href="([^"]+)">([^<]+)</a>\s+([\w\s:-]+)\s+(-|[\d\.]+[KMG]?)})
	    {
	      my ($pre, $name1, $name2, $date, $size) = ($1, $2, $3, $4, $5);
	      next if $name1 =~ m{^/} or $name1 =~ m{^\.\.};
	      $name1 =~ s{%([\da-fA-F]{2})}{pack 'c', hex $1}ge;
	      $name1 =~ s{^\./}{};
	      my $dir = 1 if $pre =~ m{"\[DIR\]"};
	      print "$pre^$name1^$date^$size\n" if $verbose > 1;
              my $t = length($name) ? "$name/$name1" : $name1;
	      if ($size eq '-' and ($dir or $name =~ m{/$}))
	        {
		  ## we must be really sure it is a directory, when we come here.
		  ## otherwise, we'll retrieve the contents of a file!
                  push @r, http_readdir($id, $url, $t);
		}
	      else
	        {
		  ## it is a file.
		  my $time = str2time($date);
		  my $len = byte_size($size);

                  #save timestamp and file in database
	          save_file($t, $id, $time);

                  push @r, [ $t , $time ];
		}
	    }
	}
    }
  else
    {
      ## we come here, whenever we stumble into an automatic index.html 
      $contents = substr($contents, 0, 500);
      warn Dumper $contents, "http_readdir: unknown HTML format";
    }

  return @r;
}

sub byte_size
{
  my ($len) = @_;
  return $len unless $len =~ m{(.*)([KMG])$};
  my ($n, $l) = ($1,$2);
  return int($n*1024)           if $l eq 'K';
  return int($1*1024*1024)      if $l eq 'M';
  return int($1*1024*1024*1024) if $l eq 'G';
  die;
}

sub ftp_readdir
{
  my ($id, $url, $name) = @_;
  $url =~ s{/+$}{};	# we add our own trailing slashes...

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
    if ($text[$i] =~ m/^([dl-]).*(\w\w\w\s+\d\d?\s+\d\d:?\d\d)\s+([\S]+)$/)
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
      if ($1 eq 'l')
        {
	  warn "symlink($t) not impl.";
	}
      else
      {
        #save timestamp and file in database
	save_file($t, $id, $time);

        push @r, [ $t , $time ];
      }
    }
  }
  return @r;
}

sub save_file
{
  my ($path, $serverid, $file_tstamp) = @_;

  $path =~ s{^/+}{};	# be sure we have no leading slashes.
  my ($fileid, $md5) = getfileid($path);
  die "save_file: md5 undef" unless defined $md5;

  if ($use_md5)
    {
      if (checkfileserver_md5($serverid, $md5))
      {
      my $sql = "UPDATE file_server SET 
		 timestamp_file = FROM_UNIXTIME(?),
		 timestamp_scanner = CURRENT_TIMESTAMP()
		 WHERE path_md5 = ? AND serverid = ?;";

      my $sth = $dbh->prepare( $sql );
		    $sth->execute( $file_tstamp, $md5, $serverid ) or die $sth->errstr;
      }  
      else
      {

      my $sql = "INSERT INTO file_server SET path_md5 = ?,
		 fileid = ?, serverid = ?,
		 timestamp_file = FROM_UNIXTIME(?), 
		 timestamp_scanner = CURRENT_TIMESTAMP();"; 
		 #convert timestamp to mysql timestamp

      my $sth = $dbh->prepare( $sql );
		    $sth->execute( $md5, $fileid, $serverid, $file_tstamp ) or die $sth->errstr;
      }
    }
  else
    {
      if (checkfileserver_fileid($serverid, $fileid))
      {
      my $sql = "UPDATE file_server SET 
		 timestamp_file = FROM_UNIXTIME(?),
		 timestamp_scanner = CURRENT_TIMESTAMP()
		 WHERE fileid = ? AND serverid = ?;";

      my $sth = $dbh->prepare( $sql );
		    $sth->execute( $file_tstamp, $fileid, $serverid ) or die $sth->errstr;
      }  
      else
      {

      my $sql = "INSERT INTO file_server SET fileid = ?,
		 serverid = ?,
		 timestamp_file = FROM_UNIXTIME(?), 
		 timestamp_scanner = CURRENT_TIMESTAMP();"; 
		 #convert timestamp to mysql timestamp

      my $sth = $dbh->prepare( $sql );
		    $sth->execute( $fileid, $serverid, $file_tstamp ) or die $sth->errstr;
      }
    }
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


# getfileid returns the id as inserted in table file and the md5sum.
#
# using md5 hashes, we still populate table file, 
# so that we can ask the database to enumerate the files 
# we have seen. Caller should still write the ids to file_server table, so that
# a reverse lookup can be done. ("list me all files matching foo on server bar")
#
sub getfileid
{
  my $path = shift;

  my $sql = "SELECT id FROM file WHERE path = " . $dbh->quote($path);

  my $ary_ref = $dbh->selectall_arrayref( $sql )
                     or die $dbh->errstr();
  my $id = $ary_ref->[0][0];

  return $id, Digest::MD5::md5_base64($path) if defined $id;
  
  $sql = "INSERT INTO file SET path = ?;";

  my $sth = $dbh->prepare( $sql );
                $sth->execute( $path ) or die $sth->err;

  $sql = "SELECT id FROM file WHERE path = " . $dbh->quote($path);

  $ary_ref = $dbh->selectall_arrayref( $sql )
                       or die $dbh->errstr();

  $id = $ary_ref->[0][0];

  return $id, Digest::MD5::md5_base64($path);
}

sub checkfileserver_fileid
{
  my ($serverid, $fileid) = @_;

  my $sql = "SELECT 1 FROM file_server WHERE fileid = $fileid AND serverid = $serverid;";
  my $ary_ref = $dbh->selectall_arrayref($sql) or die $dbh->errstr();

  return defined($ary_ref->[0]) ? 1 : 0;
}  

sub checkfileserver_md5
{
  my ($serverid, $md5) = @_;

  my $sql = "SELECT 1 FROM file_server WHERE path_md5 = '$md5' AND serverid = $serverid";
  my $ary_ref = $dbh->selectall_arrayref($sql) or die $dbh->errstr();

  return defined($ary_ref->[0]) ? 1 : 0;
}  

sub rsync_cb
{
  my ($priv, $name, $len, $mode, $mtime, @info) = @_;
  return if $name eq '.' or $name eq '..';
  if ($mode & 0x1000)	 # directories have 0 here.
    {
      save_file($name, $priv->{serverid}, $mtime);
    }
  else
    {
      printf "%03o %8d %-25s %-50s\n", ($mode & 0777), $len, scalar(localtime $mtime), $name;
    }
  return;
}

sub rsync_readdir
{
  my ($serverid, $url) = @_;

  $url =~ s{^rsync://}{}s;
  my $cred = $1 if $url =~ s{^(.*?)@}{};
  die "rsync_scan: cannot parse '$url'\n" unless $url =~ m{^([^:/]+)(:(\d*))?(.*)$};
  my ($host, $dummy, $port, $path) = ($1,$2,$3,$4);
  $port = 873 unless $port;
  $path =~ s{^/}{};

  my $peer = { addr => inet_aton($host), port => $port, serverid => $serverid };
  $peer->{pass} = $1 if $cred and $cred =~ s{:(.*)}{};
  $peer->{user} = $cred if $cred;
  return rsync_get_filelist($peer, $path, 0, \&rsync_cb, $peer);
}



#######################################################################
# rsync protocol
#######################################################################
#
# Copyright (c) 2005 Michael Schroeder (mls@suse.de)
#
# This program is licensed under the BSD license, read LICENSE.BSD
# for further information
#

sub sread {
  local *SS = shift;
  my $len = shift;
  my $ret = '';
  while ($len > 0) {
    my $r = sysread(SS, $ret, $len, length($ret));
    die("read error") unless $r;
    $len -= $r;
    die("read too much") if $r < 0;
  }
  return $ret;
}

sub swrite {
  local *SS = shift;
  my ($var, $len) = @_;
  $len = length($var) unless defined $len;
  (syswrite(SS, $var, $len) || 0) == $len || die("syswrite: $!\n");
}

sub muxread {
  local *SS = shift;
  my $len = shift;

  #print "muxread $len\n";
  while(length($rsync_muxbuf) < $len) {
    #print "muxbuf len now ".length($muxbuf)."\n";
    my $tag = '';
    $tag = sread(*SS, 4);
    $tag = unpack('V', $tag);
    my $tlen = 0+$tag & 0xffffff;
    $tag >>= 24;
    if ($tag == 7) {
      $rsync_muxbuf .= sread(*SS, $tlen);
      next;
    }
    if ($tag == 8 || $tag == 9) {
      my $msg = sread(*SS, $tlen);
      die("$msg\n") if $tag == 8;
      print "info: $msg\n";
      next;
    }
    die("unknown tag: $tag\n");
  }
  my $ret = substr($rsync_muxbuf, 0, $len);
  $rsync_muxbuf = substr($rsync_muxbuf, $len);
  return $ret;
}

sub rsync_get_filelist {
  my ($peer, $syncroot, $norecurse, $callback, $priv) = @_;
  my $syncaddr = $peer->{addr};
  my $syncport = $peer->{port};

  if (!defined($peer->{have_md4})) {
    ## why not rely on %INC here?
    $peer->{have_md4} = 0;
    eval {
      require Digest::MD4;
      $peer->{have_md4} = 1;
    };
  }
  $syncroot =~ s/^\/+//;
  my $module = $syncroot;
  $module =~ s/\/.*//;
  my $tcpproto = getprotobyname('tcp');
  socket(S, PF_INET, SOCK_STREAM, $tcpproto) || die("socket: $!\n");
  connect(S, sockaddr_in($syncport, $syncaddr)) || die("connect: $!\n");
  my $hello = "\@RSYNCD: 28\n";
  swrite(*S, $hello);
  my $buf = '';
  sysread(S, $buf, 4096);
  die("protocol error [$buf]\n") if $buf !~ /^\@RSYNCD: (\d+)\n/s;
  $peer->{rsync_protocol} = $1;
  $peer->{rsync_protocol} = 28 if $peer->{rsync_protocol} > 28;
  swrite(*S, "$module\n");
  while(1) {
    sysread(S, $buf, 4096);
    die("protocol error [$buf]\n") if $buf !~ s/\n//s;
    last if $buf eq "\@RSYNCD: OK";
    die("$buf\n") if $buf =~ /^\@ERROR/s;
    if ($buf =~ /^\@RSYNCD: AUTHREQD /) {
      die("'$module' needs authentification, but Digest::MD4 is not installed\n") unless $peer->{have_md4};
      my $user = "nobody" if !defined($peer->{user}) || $peer->{user} eq '';
      my $password = '' unless defined $peer->{password};
      my $digest = "$user ".Digest::MD4::md4_base64("\0\0\0\0$password".substr($buf, 18))."\n";
      swrite(*S, $digest);
      next;
    }
  }
  my @args = ('--server', '--sender', '-rl');
  push @args, '--exclude=/*/*' if $norecurse;
  for my $arg (@args, '.', "$syncroot/.", '') {
    swrite(*S, "$arg\n");
  }
  sread(*S, 4);	# checksum seed
  swrite(*S, "\0\0\0\0");
  my @filelist;
  my $name = '';
  my $mtime = 0;
  my $mode = 0;
  my $uid = 0;
  my $gid = 0;
  my $flags;
  while(1) {
    $flags = muxread(*S, 1);
    $flags = ord($flags);
    # printf "flags = %02x\n", $flags;
    last if $flags == 0;
    $flags |= ord(muxread(*S, 1)) << 8 if $peer->{rsync_protocol} >= 28 && ($flags & 0x04) != 0;
    my $l1 = $flags & 0x20 ? ord(muxread(*S, 1)) : 0;
    my $l2 = $flags & 0x40 ? unpack('V', muxread(*S, 4)) : ord(muxread(*S, 1));
    $name = substr($name, 0, $l1).muxread(*S, $l2);
    my $len = unpack('V', muxread(*S, 4));
    if ($len == 0xffffffff) {
      $len = unpack('V', muxread(*S, 4));
      my $len2 = unpack('V', muxread(*S, 4));
      $len += $len2 * 4294967296;
    }
    $mtime = unpack('V', muxread(*S, 4)) unless $flags & 0x80;
    $mode = unpack('V', muxread(*S, 4)) unless $flags & 0x02;
    my @info = ();
    my $mmode = $mode & 07777;
    if (($mode & 0170000) == 0100000) {
      $mmode |= 0x1000;
    } elsif (($mode & 0170000) == 0040000) {
      $mmode |= 0x0000;
    } elsif (($mode & 0170000) == 0120000) {
      $mmode |= 0x2000;
      my $ln = muxread(*S, unpack('V', muxread(*S, 4)));
      @info = ($ln);
    } else {
      print "$name: unknown mode: $mode\n";
      next;
    }
    if ($callback)
      {
        my $r = &$callback($priv, $name, $len, $mmode, $mtime, @info);
        push @filelist, $r if $r;
      }
    else
      {
        push @filelist, [$name, $len, $mmode, $mtime, @info];
      }
  }
  my $io_error = unpack('V', muxread(*S, 4));

  # rsync_send_fin
  swrite(*S, pack('V', -1));      # switch to phase 2
  swrite(*S, pack('V', -1));      # switch to phase 3
  if ($peer->{rsync_protocol} >= 24) {
    swrite(*S, pack('V', -1));    # goodbye
  }
  close(S);
  return @filelist;
}

sub cb_print
{
  my ($priv, $name, $len, $mode, $mtime, @info) = @_;
  return if $name eq '.' or $name eq '..';
  my $d = ($mode & 0x1000) ? '-' : 'd';		# directories have 0 here.
  printf "$d %03o %8d %-25s %-50s\n", ($mode & 0777), $len, scalar(localtime $mtime), $name;
  return;
}

# rsync://ftp5.gwdg.de/pub/opensuse/tools
# rsync://ftp5.gwdg.de:873/pub/opensuse/tools
sub rsync_scan
{
  my ($url, $norecurse) = @_;

  $url =~ s{^rsync://}{}s;
  my $cred = $1 if $url =~ s{^(.*?)@}{};
  die "rsync_scan: cannot parse '$url'\n" unless $url =~ m{^([^:/]+)(:(\d*))?(.*)$};
  my ($host, $dummy, $port, $path) = ($1,$2,$3,$4);
  $port = 873 unless $port;
  $path =~ s{^/}{};

  my $peer = { addr => inet_aton($host), port => $port };
  $peer->{pass} = $1 if $cred and $cred =~ s{:(.*)}{};
  $peer->{user} = $cred if $cred;
  return rsync_get_filelist($peer, $path, $norecurse, \&cb_print, $peer);
}

my @fl = rsync_scan($ARGV[0], $ARGV[1]);
print Dumper(\@fl);
