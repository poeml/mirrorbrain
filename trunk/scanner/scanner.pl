#!/usr/bin/perl -w

################################################################################
# scanner.pl daemon for working through opensuse directories.
# Copyright (C) 2006-2007 Martin Polster, Juergen Weigert, Novell Inc.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License version 2
# as published by the Free Software Foundation; 
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
# 2007-03-08, jw  - option parser added. -l -ll implemented.
#                   -d subdir done. -x, -k done.
# 2007-03-13, jw  - V0.5 option -j added. 
# 2007-03-15, jw  - V0.6, allow identifier as well as ID on command line.
#                   added recursion_delay = 2 sec, fixed rsync with -d.
# 2007-03-22, jw  - V0.7, skipping unreadable files in ftp-scan.
# 2007-03-27, jw  - V0.8a, -N, -Z and -A fully implemented.
#                   -D started, delete_file() tbd.
# 2007-03-30, jw  - V0.8b, -lll list long format all in one line for easier grep.
# 2007-04-04, jw  - V0.8c, implemented #@..@..@ url suffix for all backends.
#                          it is now in save_file, (was in rsync_readdir before)
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
my $version = '0.8c';

my $topdirs = 'distribution|tools|repositories';

# Create a user agent object
my $ua = LWP::UserAgent->new;
$ua->agent("openSUSE Scanner/$version (See http://en.opensuse.org/Mirrors/Scanner)");

my $verbose = 1;
my $use_md5 = 1;
my $all_servers = 0;
my $start_dir = '/';
my $parallel = 1;
my $list_only = 0;
my $extra_schedule_run = 0;
my $keep_dead_files = 0;
my $recursion_delay = 2;	# seconds delay per readdir_* recuursion
my $mirror_new = undef;
my $mirror_zap = 0;
my $mirror_url_add = undef;
my $mirror_url_del = undef;


exit usage() unless @ARGV;
while (defined (my $arg = shift))
  {
    if    ($arg !~ m{^-})                  { unshift @ARGV, $arg; last; }
    elsif ($arg =~ m{^(-h|--help|-\?)})    { exit usage(); }
    elsif ($arg =~ m{^-q})                 { $verbose = 0; }
    elsif ($arg =~ m{^-v})                 { $verbose++; }
    elsif ($arg =~ m{^-a})                 { $all_servers++; }
    elsif ($arg =~ m{^-j})                 { $parallel = shift; }
    elsif ($arg =~ m{^-x})                 { $extra_schedule_run++; }
    elsif ($arg =~ m{^-k})                 { $keep_dead_files++; }
    elsif ($arg =~ m{^-d})                 { $start_dir = shift; }
    elsif ($arg =~ m{^-l})                 { $list_only++; 
                                             $list_only++ if $arg =~ m{ll}; 
                                             $list_only++ if $arg =~ m{lll}; }
    elsif ($arg =~ m{^-N})                 { $mirror_new = [ shift ]; 
    				             while ($ARGV[0] and $ARGV[0] =~ m{=}) { push @$mirror_new, shift; } }
    elsif ($arg =~ m{^-Z})                 { $mirror_zap++; }
    elsif ($arg =~ m{^-A})                 { $mirror_url_add = [ @ARGV ]; @ARGV = (); }
    elsif ($arg =~ m{^-D})                 { $mirror_url_del = [ @ARGV ]; @ARGV = (); }
    elsif ($arg =~ m{^-})		   { exit usage("unknown option '$arg'"); }
  }

my %only_server_ids = map { $_ => 1 } @ARGV;

exit usage("Please specify list of server IDs (or -a for all) to scan\n") 
  unless $all_servers or %only_server_ids or $list_only or $mirror_new or $mirror_url_del or $mirror_url_add;

exit usage("-a takes no parameters (or try without -a ).\n") if $all_servers and %only_server_ids;

exit usage("-j requires a positive number") unless $parallel =~ m{^\d+$} and $parallel > 0;

my $dbh = DBI->connect( 'dbi:mysql:dbname=redirector;host=galerkin.suse.de', 'root', '',
                        { PrintError => 0 } ) or die $DBI::errstr;

my $sql = qq{SELECT * FROM server};
my $ary_ref = $dbh->selectall_hashref($sql, 'id')
		   or die $dbh->errstr();

my @scan_list;

for my $row (sort { $a->{id} <=> $b->{id} } values %$ary_ref)
  {
    next if keys %only_server_ids and !defined $only_server_ids{$row->{id}}
				  and !defined $only_server_ids{$row->{identifier}};
    delete $only_server_ids{$row->{id}};
    delete $only_server_ids{$row->{identifier}};

    if ($row->{enabled} == 1 or $list_only > 1 or $mirror_new or $mirror_zap)
      {
	push @scan_list, $row;
      }
  }

die sprintf "serverid not found: %s\n", join(', ', keys %only_server_ids) if keys %only_server_ids;

exit mirror_new($dbh, $mirror_new, \@scan_list) if defined $mirror_new;
exit mirror_zap($dbh, \@scan_list) if $mirror_zap;
exit mirror_list(\@scan_list, $list_only-1) if $list_only;
exit mirror_url($dbh, $mirror_url_add, \@scan_list, 0) if $mirror_url_add;
exit mirror_url($dbh, $mirror_url_del, \@scan_list, 1) if $mirror_url_del;

###################
# Keep in sync with "$start_dir/%" in unless ($keep_dead_files) below!
$start_dir =~ s{^/+}{};	# leading slash is implicit; leads to '' per default.
$start_dir =~ s{/+$}{};	# trailing slashes likewise. 
##################

# be sure not to parallelize if there is exactly one server to scan.
$parallel = 1 if scalar keys %only_server_ids == 1;

if ($parallel > 1)
  {
    my @worker;
    my @cmd = ($0);
    push @cmd, '-q' unless $verbose;
    push @cmd, ('-v') x ($verbose - 1) if $verbose > 1;
    push @cmd, '-x' if $extra_schedule_run;
    push @cmd, '-k' if $keep_dead_files;
    push @cmd, '-d', $start_dir if length $start_dir;
    # We must not propagate -j here.
    # All other options we should propagate.

    for my $row (@scan_list)
      {
        # check if one of the workers is idle
        my $worker_id = wait_worker(\@worker, $parallel);
	$worker[$worker_id] = { serverid => $row->{id}, pid => fork_child($worker_id, @cmd, $row->{id}) };
      }

    while (wait > -1)
      {
        print "reap\n" if $verbose;
        ;	# reap all children
      }
    exit 0;
  }

for my $row (@scan_list)
  {
    print "$row->{id}: $row->{identifier} : \n" if $verbose;

    my $start = time();
    my $file_count = rsync_readdir($row->{id}, $row->{baseurl_rsync}, $start_dir);
    if (!$file_count and $row->{baseurl_ftp})
      {
        print "no rsync, trying ftp\n" if $verbose;
        $file_count = scalar ftp_readdir($row->{id}, $row->{baseurl_ftp}, $start_dir);
      }
    if (!$file_count and $row->{baseurl})
      {
        print "no rsync, no ftp, trying http\n" if $verbose;
        $file_count = scalar http_readdir($row->{id}, $row->{baseurl}, $start_dir);
      }

    my $duration = time() - $start;
    $duration = 1 if $duration < 1;
    my $fpm = int(60*$file_count/$duration);

    unless ($keep_dead_files)
      {
	my $sql = "DELETE FROM file_server WHERE serverid = $row->{id} 
		   AND timestamp_scanner <= (SELECT last_scan FROM server 
		   WHERE id = $row->{id} limit 1)";

	if (length $start_dir)
	  {
	    ## let us hope subselects with paramaters work in mysql.
	    $sql .= " AND fileid IN (SELECT id FROM file WHERE path LIKE ?)";
	  }

	print "$sql\n" if $verbose > 1;
	# Keep in sync with $start_dir setup above!
	my $sth = $dbh->prepare( $sql );
		  $sth->execute(length($start_dir) ? "$start_dir/%" : ()) or die $sth->errstr;
      }

    unless ($extra_schedule_run)
      {
        $sql = "UPDATE server SET last_scan = CURRENT_TIMESTAMP, scan_fpm = $fpm WHERE id = $row->{id};";
	print "$sql\n" if $verbose > 1;
        my $sth = $dbh->prepare( $sql );
                  $sth->execute() or die $sth->err;
      }

    print "server $row->{id}, $file_count files.\n" if $verbose > 0;
  }

$dbh->disconnect();
exit;
###################################################################################################

sub usage
{
  my ($msg) = @_;

  print STDERR qq{$0 V$version usage:

scanner [options] [mirror_ids ...]

  -v        Be more verbose (Default: $verbose).
  -q        Be quiet.
  -l        Do not scan. List enabled mirrors only.
  -ll       As -l, but include disabled mirrors and print urls.
  -lll      As -ll, but all in one grep-friendly line.

  -a        Scan all mirrors. Alternative to providing a list of mirror_ids.
  -d dir    Scan only in dir under mirror's baseurl. 
            Default: start at baseurl. Consider using -x and or -k with -d .
  -x        Extra-Schedule run. Do not update 'scanner.last_scan' tstamp.
            Default: 'scanner.last_scan' is updated after each run.
  -k        Keep dead files. Default: Entries not found again are removed.

  -N url mirror_id
            Add (or replace) a new url to the named mirror.
  -N enable=1 mirror_id
  	    Enable a mirror.
  -N url rsync=url ftp=url score=100 country=de region=europe name=identifier
            Create a new mirror.
	    Scanned path names (starting after url) should start with 
	    '$topdirs'.
	    Urls may be suffixed with #\@^foo/\@bar/\@ to modify scanned path names.

  -Z mirror_id
	    Delete the named mirror completly from the database.
	    (Use "-N enable=0 mirror_id" to disable a mirror.
	     Use "-N http='' mirror_id" to delete an url.)

  -A mirror_id path
  -A url
  	    Add an url to the mirror (as if it was found by scanning).
	    If only one parameter is given, mirror_id is derived from url.

  -j N      Run up to N scanner queries in parallel.

Both, names(identifier) and numbers(id) are accepted as mirror_ids.
};
 my $hide = qq{
            
  -D mirror_id path
  -D url
  	    As -A, but removes an url from a mirror.
};

  print STDERR "\nERROR: $msg\n" if $msg;
  return 0;
}

sub mirror_list
{
  my ($list, $longflag) = @_;
  print " id name                      scan_speed   last_scan\n";
  print "---+-------------------------+-----------+-------------\n";
  my $nl = ($longflag > 1) ? "\t" : "\n";
  for my $row (@$list)
    {
      printf "%3d %-30s %5d   %s$nl", $row->{id}, $row->{identifier}||'--', $row->{scan_fpm}||0, $row->{last_scan}||'';
      if ($longflag)
	{
	  print "\t$row->{baseurl_rsync}$nl" if length($row->{baseurl_rsync}||'') > 0;
	  print "\t$row->{baseurl_ftp}$nl"   if length($row->{baseurl_ftp}||'') > 0;
	  print "\t$row->{baseurl}$nl"       if length($row->{baseurl}||'') > 0;
	  printf "\tscore=%d country=%s region=%s enabled=%d$nl", 
	  	$row->{score}||0, $row->{country}||'', $row->{region}||'', $row->{enabled}||0;
	  print "\n";
	}
    }
  return 0;
}

sub mirror_zap
{
  my ($dbh, $list) = @_;
  my $ids = join(',', map { $_->{id} } @$list);
  die "mirror_zap: list empty\n" unless $ids;

  my $sql = "DELETE FROM server WHERE id IN ($ids)";
  print "$sql\n" if $verbose;
  $dbh->do($sql) or die "$sql: ".$dbh->errstr;
  $sql = "DELETE FROM file_server WHERE serverid IN ($ids)";
  print "$sql\n" if $verbose;
  $dbh->do($sql) or die "$sql: ".$dbh->errstr;
}

sub mirror_new
{
  my ($dbh, $mirror_new, $old) = @_;

  my $fields;
  my %proto2field = 
    (
      http => 'baseurl', ftp => 'baseurl_ftp', rsync => 'baseurl_rsync', 'rsync:' => 'baseurl_rsync',
      name => 'identifier'
    );

  my $name;
  if ($mirror_new->[0] =~ m{^(http|ftp|rsync:?)://([^/]+)/(.*)$})
    {
      my ($proto, $host, $path) = ($1,$2,$3);

      if ($path !~ m{#} and $path =~ s{(^|/)($topdirs)(/.*?)?$}{})
	{
	  warn qq{path truncated before component "/$2/": $path\n};
	  warn qq{Press Enter to continue, CTRL-C to abort\n};
	  <STDIN>;
	}
      $fields->{$proto2field{$proto}} = "$proto://$host/$path";
      shift @$mirror_new;
      $name = $host;
    }

  die "mirror_new: try (http|ftp|rsync)://hostname/path\n (seen '$mirror_new')\n"
    unless $name or scalar @$mirror_new;

  for my $i (0..$#$mirror_new)
    {
      die "mirror_new: cannot parse $mirror_new->[$i]" unless $mirror_new->[$i] =~ m{^([^=]+)=(.*)$};
      my ($key, $val) = ($1, $2);
      $key = $proto2field{$key} if defined $proto2field{$key};
      $fields->{$key} = $val;
    }

  if ($#$old == 0)	# exactly one id given.
    {
      for my $k (keys %$fields)
        {
	  delete $fields->{$k} if $fields->{$k} eq ($old->[0]{$k}||'');
	}

      unless (keys %$fields)
        {
          warn "nothing changes.\n";
	  return 1;
	}
      warn "updating id=$old->[0]{id}: @{[keys %$fields]}\n";
      my $sql = "UPDATE server SET " . 
      	join(', ', map { "$_ = ".$dbh->quote($fields->{$_}) } keys %$fields) . 
	" WHERE id = $old->[0]{id}";

      print "$sql\n" if $verbose;
      $dbh->do($sql) or die "$sql: ".$dbh->errstr;
    }
  else
    {
      $fields->{identifier} ||= $name||'';
      $fields->{country} = $1 if !$fields->{country} and $fields->{identifier} =~ m{\.(\w\w)$};
      die "cannot create new mirror without name or url.\n" unless $fields->{identifier};

      $fields->{score} = 100 unless defined $fields->{score};
      $fields->{enabled} = 1 unless defined $fields->{enabled};
      my $dup_id;
      for my $o (@$old)
        {
	  $dup_id = "$o->{id}: identifier"    if lc $o->{identifier} eq lc $fields->{identifier};
	  $dup_id = "$o->{id}: baseurl"       if $fields->{baseurl} and $fields->{baseurl} eq ($o->{baseurl}||'');
	  $dup_id = "$o->{id}: baseurl_ftp"   if $fields->{baseurl_ftp} and $fields->{baseurl_ftp} eq ($o->{baseurl_ftp}||'');
	  $dup_id = "$o->{id}: baseurl_rsync" if $fields->{baseurl_rsync} and $fields->{baseurl_rsync} eq ($o->{baseurl_rsync}||'');
	}
      die "new mirror and existing $dup_id is identical\n" if $dup_id;

      my $sql = "INSERT INTO server SET " . 
      	join(', ', map { "$_ = ".$dbh->quote($fields->{$_}) } keys %$fields);

      print "$sql\n" if $verbose;
      $dbh->do($sql) or die "$sql: ".$dbh->errstr;
    }
  return 0;
}

sub mirror_url
{
  my ($dbh, $list, $ml, $del) = @_;
  my $act = $del ? 'del' : 'add';

  while (my $item = shift @$list)
    {
      my ($p, $id);
      if ($item =~ m{/})	# aha, it is should be an url
        {
	  die "mirror_url $act: cannot parse '$item'\n" unless $item =~ m{^(http|ftp|rsync:?)://([^/]+)/(.*)$};
	  my ($proto, $host, $path) = ($1,$2,$3);
	  my $base = "$proto://$host";
	  for my $m (@$ml)
	    {
	      ## FIXME: this does not work, if baseurl* ends in #@..@..@
	      $p = $1 if $m->{baseurl}       and $item =~ m{^\Q$m->{baseurl}\E(.*)};
	      $p = $1 if $m->{baseurl_ftp}   and $item =~ m{^\Q$m->{baseurl_ftp}\E(.*)};
	      $p = $1 if $m->{baseurl_rsync} and $item =~ m{^\Q$m->{baseurl_rsync}\E(.*)};
	      if ($p)
	        {
		  $id = $m->{id};
		  last;
		}
	    }
	  die "mirror_url $act: could not find mirror for url '$item'\n" unless defined $id;
	}
      else	# aha, it is id plus path.
        {
	  for my $m (@$ml)
	    {
	      if ($m->{id} eq $item || $m->{identifier} eq $item)
	        {
	          $id = $m->{id};
	          $p = shift @$list;
	          last;
		}
	    }
	  die "mirror_url $act: unknown mirror '$item'\n" unless defined $id;
	}

      $p =~ s{^/+}{} if $p;
      die "mirror_url $act: item=$item, no path.\n" unless $p;

      print "mirror_url $act $id '$p'\n" if $verbose;
      if ($del)
        {
	  delete_file($dbh, $id, $p);
	}
      else
        {
	  save_file($p, $id, time);
	}
    }
  return 0;
}

sub wait_worker
{
  my ($a, $n) = @_;
  die if $n < 1;
  my %pids;

  for (;;)
    {
      for (my $i = 0; $i < $n; $i++)
	{
	  return $i unless $a->[$i];
	  my $p = $a->[$i]{pid};
	  unless (kill(0, $p)) 		# already dead? okay take him home.
	    {
	      print "kill(0, $p) returned 0. reusing $i!\n" if $verbose;
	      undef $a->[$i];	
	      return $i;
	    }
	  $pids{$p} = $i;		# not? okay wait.
	}
      my $p = wait;
      if (defined(my $i = $pids{$p}))
        {
	  print "[#$i, id=$a->[$i]{serverid} pid=$p exit: $?]\n" if $verbose;
	  undef $a->[$i];
	  return $i;			# now, been there, done that.
	}
      # $p = -1 or other silly things...
      warn "wait failed: $!, $?\n";
      die "wait failed" if $p < 0;
    }
}

sub fork_child
{
  my ($idx, @args) = @_;
  if (my $p = fork())
    {
      # parent 
      print "worker $idx, pid=$p start.\n" if $verbose > 1;
      return $p;
    }
  my $cmd = shift @args;
  exec { $cmd } "scanner [#$idx]", @args;	# ourselves with a false name and some data.
}

# http://ftp1.opensuse.org/repositories/#@^@repositories/@@
sub http_readdir
{
  my ($id, $url, $name) = @_;

  my $urlraw = $url;
  my $re  = $1 if $url =~ s{#(.*?)$}{};
  print "http_readdir: url=$url re=$re\n" if $verbose;
  $url =~ s{/+$}{};	# we add our own trailing slashes...
  $name =~ s{/+$}{};

  my @r;
  print "$id $url/$name\n" if $verbose;
  my $contents = cont("$url/$name");
  if ($contents =~ s{^.*<pre>.*<a href="\?C=.;O=.">}{}s)
    {
      ## good, we know that one. It is a standard apache dir-listing.
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
                  sleep($recursion_delay) if $recursion_delay;
                  push @r, http_readdir($id, $urlraw, $t);
		}
	      else
	        {
		  ## it is a file.
		  my $time = str2time($date);
		  my $len = byte_size($size);

                  #save timestamp and file in database
	          save_file($t, $id, $time, $re);

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

  my $urlraw = $url;
  my $re  = $1 if $url =~ s{#(.*?)$}{};
  $url =~ s{/+$}{};	# we add our own trailing slashes...

  print "$id $url/$name\n" if $verbose;
  my $content = cont("$url/$name");
  
  if ($content =~ m/^\d\d\d\s/)		# some FTP status code? Not good.
  {
    print $content if $verbose > 2;
    return;
  }  

  print $content."\n" if $verbose > 2;

  my @text = split( "\n", $content );

  my @r;
  for my $i (0..$#text)
    {
      if ($text[$i] =~ m/^([dl-])(.........).*(\w\w\w\s+\d\d?\s+\d\d:?\d\d)\s+([\S]+)$/)
	{
	  my ($type, $mode, $timestamp, $fname) = ($1, $2, $3, $4);
	  next if $fname eq "." or $fname eq "..";

	  #convert to timestamp
	  my $time = str2time($timestamp);
	    
	  my $t = length($name) ? "$name/$fname" : $fname;

	  if ($type eq "d")
	    {
	      if ($mode !~ m{r.xr.xr.x})
		{
		  print "bad mode $mode, skipping directory $fname\n" if $verbose;
		  next;
		}
	      sleep($recursion_delay) if $recursion_delay;
	      push @r, ftp_readdir($id, $urlraw, $t);
	    }
	  if ($type eq 'l')
	    {
	      warn "symlink($t) not impl.";
	    }
	  else
	    {
	      if ($mode !~ m{r..r..r..})
		{
		  print "bad mode $mode, skipping file $fname\n" if $verbose;
		  next;
		}
	      #save timestamp and file in database
	      save_file($t, $id, $time, $re);

	      push @r, [ $t , $time ];
	    }
	}
    }
  return @r;
}

sub save_file
{
  my ($path, $serverid, $file_tstamp, $re) = @_;

  #
  # optional patch the file names by adding or removing components.
  # you never know what strange paths mirror admins choose.
  #

  if ($re and $re =~ m{@([^@]*)@([^@]*)})
    {
      print "save_file: $path + #$re -> " if $verbose > 2;
      my ($m, $r) = ($1, $2);
      $path =~ s{$m}{$r};
      print "$path\n" if $verbose > 2;
    }

  $path =~ s{^/+}{};	# be sure we have no leading slashes.
  $path =~ s{//+}{/}g;	# double slashes easily fool md5sums. Avoid them.


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
  return $path;
}

sub delete_file
{
  my ($dbh, $serverid, $path) = @_;
  warn "FIXME: delete_file() not impl.\n";
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
# E.g. Option -d needs to list all files below a certain path prefix.
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

  if ($priv->{subdir})
    {
      # subdir is expected not to start or end in slashes.
      $name = $priv->{subdir} . '/' . $name;
    }

  if ($mode & 0x1000)	 # directories have 0 here.
    {
      if ($mode & 004)		# readable for the world is good.
        {
          $name = save_file($name, $priv->{serverid}, $mtime, $priv->{re});
          $priv->{counter}++;
	  printf "rsync(%d) ADD: %03o %10d %-25s %-50s\n", $priv->{serverid}, ($mode & 0777), $len, scalar(localtime $mtime), $name if $verbose > 2;
	}
      else 
        {
	  printf "rsync(%d) skip: %03o %10d %-25s %-50s\n", $priv->{serverid}, ($mode & 0777), $len, scalar(localtime $mtime), $name if $verbose > 1;
	}
    }
  elsif ($verbose)
    {
      printf "rsync(%d) dir: %03o %10d %-25s %-50s\n", $priv->{serverid}, ($mode & 0777), $len, scalar(localtime $mtime), $name;
    }
  return;
}

# rsync://ftp.sunet.se/pub/Linux/distributions/opensuse/#@^opensuse/@@
sub rsync_readdir
{
  my ($serverid, $url, $d) = @_;
  return 0 unless $url;

  $url =~ s{^rsync://}{}s;
  my $re  = $1 if $url =~ s{#(.*?)$}{};
  my $cred = $1 if $url =~ s{^(.*?)@}{};
  die "rsync_readdir: cannot parse url '$url'\n" unless $url =~ m{^([^:/]+)(:(\d*))?(.*)$};
  my ($host, $dummy, $port, $path) = ($1,$2,$3,$4);
  $port = 873 unless $port;
  $path =~ s{^/+}{};

  my $peer = { addr => inet_aton($host), port => $port, serverid => $serverid };
  $peer->{re} = $re if $re;
  $peer->{pass} = $1 if $cred and $cred =~ s{:(.*)}{};
  $peer->{user} = $cred if $cred;
  $peer->{subdir} = $d if length $d;
  $path .= "/". $d if length $d;
  rsync_get_filelist($peer, $path, 0, \&rsync_cb, $peer);
  return $peer->{counter};
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
      warn("tag=8 $msg\n") if $tag == 8;
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
