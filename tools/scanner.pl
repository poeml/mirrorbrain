#!/usr/bin/perl -w

################################################################################
# scanner.pl -- script that crawls through mirror file trees.
#
# Copyright 2006,2007,2008,2009,2010,2011,2012,2013,2014
#           Martin Polster, Juergen Weigert, Peter Poeml, Novell Inc.
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

#######################################################################
# rsync protocol implementation
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
use Net::FTP;
use Net::Domain;
use Data::Dumper;
use Time::HiRes;
use Socket;
use bytes;
use Config::IniFiles;
use Time::HiRes qw(gettimeofday);
use Encode;
use Digest::SHA qw(sha256_hex);

my $version = '2.19.2';
my $verbose = 2;
my $sqlverbose = 1;

$SIG{'PIPE'} = 'IGNORE';

$SIG{__DIE__} = sub {
  my @a = ((caller 3)[1..3], '=>', (caller 2)[1..3], '=>', (caller 1)[1..3]);
  print "__DIE__: (@a)\n";
  die @_;
};

$SIG{USR1} = sub { $verbose++; warn "sigusr1 seen. ++verbose = $verbose\n"; };
$SIG{USR2} = sub { $verbose--; warn "sigusr2 seen. --verbose = $verbose\n"; };

$ENV{FTP_PASSIVE} = 1;	# used in LWP only, Net::FTP ignores this.


# Create a user agent object
my $ua = LWP::UserAgent->new;
$ua->agent("MirrorBrain Scanner/$version (See http://mirrorbrain.org/scanner_info)");

my $rsync_muxbuf = '';
my $all_servers = 0;
my $start_dir = '/';
my $parallel = 1;
my $list_only = 0;
my $recursion_delay = 0;	# seconds delay per *_readdir recuursion
my $force_scan = 0;
my $enable_after_scan = 0;
my $cfgfile = '/etc/mirrorbrain.conf';
my $brain_instance = '';

# save prepared statements
my $sth_update;
my $sth_insert_rel;
my $sth_select_file;
my $sth_insert_file;

my $gig2 = 1<<31; # 2*1024*1024*1024 == 2^1 * 2^10 * 2^10 * 2^10 = 2^31

# these two vars are used in the largefile_check's http request callback to end
# transmission after a maximum amount of data (specified by $http_slice_counter)
my $http_size_hint;
my $http_slice_counter;

# directories to be included from top-level
my @top_include_list;

my @exclude_list;
my @exclude_list_rsync;
# default excludes:
push @exclude_list, '/.~tmp~/';
push @exclude_list_rsync, '*/.~tmp~/';
push @exclude_list_rsync, '/.~tmp~/';

my $ftp_timer_global;

exit usage() unless @ARGV;
while (defined (my $arg = shift)) {
	if    ($arg !~ m{^-})                  { unshift @ARGV, $arg; last; }
	elsif ($arg =~ m{^(-h|--help|-\?)})    { exit usage(); }
	elsif ($arg =~ m{^(-I|--top-include)}) { push @top_include_list, shift; }
	elsif ($arg =~ m{^--exclude$})         { push @exclude_list, shift; }
	elsif ($arg =~ m{^--exclude-rsync$})   { push @exclude_list_rsync, shift; }
	elsif ($arg =~ m{^-q})                 { $verbose--; }
	elsif ($arg =~ m{^-v})                 { $verbose++; }
	elsif ($arg =~ m{^-S})                 { $sqlverbose++; }
	elsif ($arg =~ m{^-a})                 { $all_servers++; }
	elsif ($arg =~ m{^-j})                 { $parallel = shift; }
	elsif ($arg =~ m{^-e})                 { $enable_after_scan++; }
	elsif ($arg =~ m{^-f})                 { $force_scan++; }
	elsif ($arg =~ m{^-d})                 { $start_dir = shift; }
	elsif ($arg =~ m{^--config})           { $cfgfile = shift; }
	elsif ($arg =~ m{^-b})                 { $brain_instance = shift; }
	elsif ($arg =~ m{^-l})                 { $list_only++;
						 $list_only++ if $arg =~ m{ll};
						 $list_only++ if $arg =~ m{lll}; }
	elsif ($arg =~ m{^-})		       { exit usage("unknown option '$arg'"); }
}


# read the configuration
my $cfg = new Config::IniFiles( -file => $cfgfile );
$cfg->SectionExists('general') or die 'no [general] section in config file';

# if the instance wasn't specified with -b, we use the first of the defined
# instances
my @brain_instances = split(/, /, $cfg->val('general', 'instances'));
$brain_instance = $brain_instances[0] unless $brain_instance;
$cfg->SectionExists($brain_instance) or die 'no [' . $brain_instance . '] section in config file';


my $db_driver = 'mysql'; # backwards compatible default
$db_driver = $cfg->val($brain_instance, 'dbdriver')
		if $cfg->val($brain_instance, 'dbdriver');

my $db_port = 'not set';
if($db_driver eq 'Pg' or $db_driver eq 'postgres' or $db_driver eq 'postgresql') {
  $db_port = '5432';
  $db_driver = 'Pg';
}
elsif($db_driver eq 'mysql') {
    $db_port = '3306';
}
else { die 'unknown dbddriver "' . $db_driver . '" in config file'; }

$db_port = $cfg->val($brain_instance, 'dbport')
		if $cfg->val($brain_instance, 'dbport');

my $db_cred = { dbi => 'dbi:' .  $db_driver
                              . ':dbname=' . $cfg->val( $brain_instance, 'dbname')
                              . ';host='   . $cfg->val( $brain_instance, 'dbhost')
                              . ';port='   . $db_port,
                user => $cfg->val( $brain_instance, 'dbuser'),
                pass => $cfg->val( $brain_instance, 'dbpass'),
                opt => { PrintError => 0 } };


my %only_server_ids = map { $_ => 1 } @ARGV;

exit usage("Please specify list of server IDs (or -a for all) to scan\n")
  unless $all_servers or %only_server_ids or $list_only;

exit usage("-a takes no parameters (or try without -a ).\n") if $all_servers and %only_server_ids;

exit usage("-e is useless without -f\n") if $enable_after_scan and !$force_scan;

exit usage("-j requires a positive number") unless $parallel =~ m{^\d+$} and $parallel > 0;

my $dbh = DBI->connect( $db_cred->{dbi}, $db_cred->{user}, $db_cred->{pass}, $db_cred->{opt}) or die $DBI::errstr;

# we fetch last_scan timestamp as epoch, because below we want to sort by it.
my $sql = qq{SELECT id, identifier, baseurl, baseurl_ftp, baseurl_rsync, enabled, extract(epoch from last_scan) as last_scan FROM server WHERE country != '**' };
print "$sql\n" if $sqlverbose;
my $ary_ref = $dbh->selectall_hashref($sql, 'id')
		   or die $DBI::errstr;

my @scan_list;

for my $row(sort { int(\$a->{last_scan}) <=> int(\$b->{last_scan}) } values %$ary_ref) {
  if(keys %only_server_ids) {
    next if !defined $only_server_ids{$row->{id}} and !defined $only_server_ids{$row->{identifier}};

    # keep some keys in %only_server_ids!
    undef $only_server_ids{$row->{id}};
    undef $only_server_ids{$row->{identifier}};
  }

  if($row->{enabled} == 1 or $force_scan or $list_only > 1) {
    push @scan_list, $row;
  }
}
#print Dumper \%only_server_ids, \@scan_list;

if(scalar(keys %only_server_ids) > 2 * scalar(@scan_list)) {
  # print Dumper \%only_server_ids, \@scan_list;
  warn "You specified some disabled mirror_ids, use -f to scan them all.\n";
  sleep 2 if scalar @scan_list;
}

my @missing = grep { defined $only_server_ids{$_} } keys %only_server_ids;
die sprintf "serverid not found: %s\n", @missing if @missing;

exit mirror_list(\@scan_list, $list_only-1) if $list_only;

###################
$start_dir =~ s{^/+}{};	# leading slash is implicit; leads to '' per default.
$start_dir =~ s{/+$}{};	# trailing slashes likewise.
##################

# be sure not to parallelize if there is exactly one server to scan.
$parallel = 1 if scalar @scan_list == 1;

if ($parallel > 1) {
  my @worker;
  my @cmd = ($0);
  push @cmd, '-b', $brain_instance;
  push @cmd, '-q' unless $verbose;
  push @cmd, ('-v') x ($verbose - 1) if $verbose > 1;
  push @cmd, ('-q') x (-($verbose - 1)) if $verbose < 0;
  foreach my $item(@top_include_list) {
    push @cmd, '-I', $item;
  }
  foreach my $item(@exclude_list) {
    push @cmd, '--exclude', $item;
  }
  foreach my $item(@exclude_list_rsync) {
    push @cmd, '--exclude-rsync', $item;
  }
  push @cmd, '-f' if $force_scan;
  push @cmd, '-e' if $enable_after_scan;
  push @cmd, '-d', $start_dir if length $start_dir;
  # We must not propagate -j here.
  # All other options we should propagate.

  for my $row (@scan_list) {
  # check if one of the workers is idle
    my $worker_id = wait_worker(\@worker, $parallel);
    $worker[$worker_id] = { identifier => $row->{identifier}, serverid => $row->{id}, pid => fork_child($worker_id, @cmd, $row->{identifier}) };
  }

  while (wait > -1) {
    print "reap\n" if $verbose > 1;
    ;	# reap all children
  }
  exit 0;
}

my %db_files_hash_id_map;
my @new_file_hashes;

my $sql_server_files = "SELECT file_id, path_hash FROM server_files
          JOIN files on file_id = id
          WHERE ? = server_id";
$sql_server_files = $sql_server_files . " AND path like '$start_dir/%'" if $start_dir;
my $smh_server_files = $dbh->prepare($sql_server_files) or die "$sql_server_files: ".$DBI::errstr;


for my $row (@scan_list) {
  print localtime(time) . " $row->{identifier}: starting\n" if $verbose > 0;

  print "$sql_server_files, $row->{id}\n" if $sqlverbose;
  $smh_server_files->execute($row->{id}) or die "$sql_server_files: ".$DBI::errstr;

  %db_files_hash_id_map = ();
  @new_file_hashes      = ();

  while ( my @r = $smh_server_files->fetchrow_array ) {
    $db_files_hash_id_map{$r[1]} = $r[0];
  }
  $smh_server_files->finish;

  my $initial_file_count = keys %db_files_hash_id_map;
  if(length $start_dir) {
    print localtime(time) . " $row->{identifier}: files in '$start_dir' before scan: $initial_file_count\n"
      if $verbose > 0;
  } else {
    print localtime(time) . " $row->{identifier}: total files before scan: $initial_file_count\n"
      if $verbose > 0;
  }

  my $start = int(gettimeofday * 1000);
  my $file_count = rsync_readdir($row->{identifier}, $row->{id}, $row->{baseurl_rsync}, $start_dir);
  if(!$file_count and $row->{baseurl_ftp}) {
    print localtime(time) . " $row->{identifier}: no rsync, trying ftp\n" if $verbose > 1;
    $file_count = scalar ftp_readdir($row->{identifier}, $row->{id}, $row->{baseurl_ftp}, time, $start_dir);
  }
  if(!$file_count and $row->{baseurl}) {
    print localtime(time) . " $row->{identifier}: no rsync, no ftp, trying http\n" if $verbose > 1;
    $file_count = scalar http_readdir($row->{identifier}, $row->{id}, $row->{baseurl}, $start_dir);
  }

  my $duration = (int(gettimeofday * 1000) - $start) / 1000;
  if (!$duration) { $duration = 1; }
  if (!$file_count) { $file_count = 0; }

  my $fpm = int(60*$file_count/$duration);

  print localtime(time) . " $row->{identifier}: scanned $file_count files ("
         . int($fpm/60) . "/s) in "
         . int($duration) . "s\n" if $verbose > 0;

  $start = time();

  print localtime(time) . " $row->{identifier}: purging old files\n" if $verbose > 1;
  my @purge_ids = sort { $a lt $b } values %db_files_hash_id_map;
  my $purge_file_count = @purge_ids;
  print localtime(time) . " $row->{identifier}: files to be purged: $purge_file_count\n" if $verbose > 0;
  # to this point save_file() would remove all relevant files from %db_files_hash_id_map
  if ($purge_file_count) {
    my $sql_values = join( ',', ("($row->{id}, ?)") x $purge_file_count );
    $sql = "DELETE FROM server_files WHERE (server_id, file_id) in ($sql_values)";
    my $smh = $dbh->prepare($sql) or die substr($sql,0,200) . "...: ".$DBI::errstr;
    print substr($sql,0,200) . "...(". $purge_file_count .")\n" if $sqlverbose;
    $smh->execute(@purge_ids) or die substr($sql,0,200) . "...: ".$DBI::errstr;
    $smh->finish;
  }
  
  my $new_file_count = @new_file_hashes;
  print localtime(time) . " $row->{identifier}: files to be inserted: $new_file_count\n" if $verbose > 0;
  if ($new_file_count) {
    my $sql_values = join( ',', ('?') x $new_file_count );
    $sql = "INSERT INTO server_files (server_id, file_id) SELECT ?, id FROM files WHERE encode(path_hash,'hex') IN ($sql_values) ORDER BY id";
    my $smh = $dbh->prepare($sql) or die substr($sql,0,200) . "...: ".$DBI::errstr;
    print substr($sql,0,200) . "...($row->{id},{". $new_file_count ."})\n" if $sqlverbose;
    $smh->execute($row->{id}, @new_file_hashes) or die substr($sql,0,200) . "...($row->{id},{". $new_file_count ."}): ".$DBI::errstr;
    $smh->finish;
  }    

  $sql = "SELECT COUNT(*) FROM server_files WHERE $row->{id} = server_id;";
  print "$sql\n" if $sqlverbose;

  if(length $start_dir) {
    print localtime(time) . " $row->{identifier}: total files in '$start_dir' after scan: $file_count " .
          "(delta: " . ($file_count - $initial_file_count) . ")\n" if $verbose > -1;
  } else {
    $ary_ref = $dbh->selectall_arrayref($sql) or die $dbh->errstr();
    $file_count = defined($ary_ref->[0]) ? $ary_ref->[0][0] : 0;
    print localtime(time) . " $row->{identifier}: total files after scan: $file_count " .
          "(delta: " . ($file_count - $initial_file_count) . ")\n" if $verbose > -1;
  }

  $duration = time() - $start;
  print localtime(time) . " $row->{identifier}: purged old files in " . $duration . "s.\n" if $verbose > 0;


  # update the last_scan timestamp; but only if we did a complete scan.
  unless ($start_dir) {
    $sql = "UPDATE server SET last_scan = NOW(), scan_fpm = $fpm WHERE id = $row->{id};";
    print "$sql\n" if $sqlverbose;
    my $sth = $dbh->prepare( $sql );
    $sth->execute() or die "$row->{identifier}: $DBI::errstr";
  }

  if($enable_after_scan && $file_count > 1 && !$row->{enabled}) {
    $sql = "UPDATE server SET enabled = '1' WHERE id = $row->{id};";
    print "$sql\n" if $sqlverbose;
    my $sth = $dbh->prepare( $sql );
    $sth->execute() or die "$row->{identifier}: $DBI::errstr";
    print localtime(time) . " $row->{identifier}: now enabled.\n" if $verbose > 0;
  }

  print localtime(time) . " $row->{identifier}: done.\n" if $verbose > 0;
}

$dbh->disconnect();
exit 0;
###################################################################################################



sub usage
{
  my ($msg) = @_;

  print STDERR qq{$0 V$version usage:

scanner [options] [mirror_ids ...]

  -b        MirrorBrain instance to use
            Default: the first which is defined in the config.
  -v        Be more verbose (Default: $verbose).
  -S        Show SQL statements.
  -q        Be quiet.
  -l        Do not scan. List enabled mirrors only.
  -ll       As -l, but include disabled mirrors and print urls.
  -lll      As -ll, but all in one grep-friendly line.

  -a        Scan all enabled mirrors. Alternative to providing a list of mirror_ids.
  -e        Enable mirror, after it was scanned. Useful with -f.
  -f        Force. Scan listed mirror_ids even if they are not enabled.
  -d dir    Scan only in dir under mirror's baseurl.
            Default: start at baseurl.

  -j N      Run up to N scanner queries in parallel.

  --exclude regexp
            Define pattern(s) for path names to ignore. Paths matching this pattern
            will not be recursed into (thus saving resources) and also, when
            matching a file, not added into the database.
            This option is effective only for scans via HTTP/FTP. For rsync,
            use the --exclude-rsync option (due to different patterns used there).
            Here, regular expressions are used.
            Path names don't start with a slash; thus, if the regexp starts with a slash
            it will not match at the top-level directory.
            Option can be repeated.
            Default: @exclude_list
  --exclude-rsync pattern
            Similar like --exclude, but used (only) for rsync scans.
            For HTTP/FTP, use the --exclude option (due to different patterns
            used there).
            The patterns are rsync(1) patterns. Option can be repeated.
            Default: @exclude_list_rsync

  -T dir    Directory to be scanned at the top level; option can be repeated.

Both, names(identifier) and numbers(id) are accepted as mirror_ids.
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
  for my $row(@$list) {
    printf "%3d %-30s %5d   %s$nl", $row->{id}, $row->{identifier}||'--', $row->{scan_fpm}||0, $row->{last_scan}||'';
    if($longflag) {
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





sub wait_worker
{
  my ($a, $n) = @_;
  die if $n < 1;
  my %pids;

  for(;;) {
    for(my $i = 0; $i < $n; $i++) {
      return $i unless $a->[$i];
      my $p = $a->[$i]{pid};
      unless (kill(0, $p)) {  # already dead? okay take him home.
        print "kill(0, $p) returned 0. reusing $i!\n" if $verbose;
        undef $a->[$i];
        return $i;
      }
      $pids{$p} = $i; # not? okay wait.
    }
    my $p = wait;
    my $rc = $?;
    if(defined(my $i = $pids{$p})) {
      if (($verbose > 1) || ($rc != 0)) {
        print "$a->[$i]{identifier}: [#$i, id=$a->[$i]{serverid} pid=$p exit: $?]\n";
      }
      undef $a->[$i];
      return $i;  # now, been there, done that.
    }
    # $p = -1 or other silly things...
    warn "wait failed: $!, $?\n";
    die "wait failed" if $p < 0;
  }
}



sub fork_child
{
  my ($idx, @args) = @_;
  if (my $p = fork()) {
  # parent
    print "worker $idx, pid=$p start.\n" if $verbose > 1;
    return $p;
  }
  my $cmd = shift @args;
  exec { $cmd } "scanner [#$idx]", @args; # ourselves with a false name and some data.
}



# http://ftp1.opensuse.org/repositories/#@^@repositories/@@
sub http_readdir
{
  my ($identifier, $id, $url, $name) = @_;

  my $item;

  my $urlraw = $url;
  my $re = ''; $re = $1 if $url =~ s{#(.*?)$}{};
  print "$identifier: http_readdir: url=$url re=$re\n" if $verbose > 2;
  $url =~ s{/+$}{};	# we add our own trailing slashes...
  $name =~ s{/+$}{};

  # are we looking at a top-level directory name?
  # (we recognize it by not containing slashes)
  my $attop = 0;
  $attop = 1 if (length $name) && !($name =~ "/");
  if ($attop && scalar(@top_include_list)) {
    my $included = 0;
    foreach my $item(@top_include_list) {
      if ($name =~ $item) {
        $included = 1;
      }
    }
    if (!$included) {
      print "$identifier: not in top_include_list: $name\n" if $verbose > 1;
      return;
    }
  }

  foreach $item(@exclude_list) {
    if("$name/" =~ $item) {
      print "$identifier: ignore match: $name matches ignored item $item, skipped.\n" if $verbose > 1;
      return;
    }
  }

  my @r;
  print "$identifier: http dir: $url/$name\n" if $verbose > 2;
  print "$identifier: http dir: $name\n" if $verbose == 2;
  my ($res, $contents) = cont("$url/$name/?F=1");
  if(!$res) {
    print "$url/$name/?F=1: error \"$contents\" fetching index, skipped.\n" if $verbose;
    return;
  }
  if($contents =~ s{^.*<(PRE|pre|table)>.*<(a href|A HREF)="\?(N=A|C=.*;O=)[^"]*">}{}s) {
    ##     _                     _
    ##    / \   _ __   __ _  ___| |__   ___
    ##   / _ \ | '_ \ / _` |/ __| '_ \ / _ \
    ##  / ___ \| |_) | (_| | (__| | | |  __/
    ## /_/   \_\ .__/ \__,_|\___|_| |_|\___|
    ##         |_|
    ## good, we know that one. It is a standard apache dir-listing.
    ##
    ## bad, apache shows symlinks as a copy of the file or dir they point to.
    ## no way to avoid duplicate crawls except by defining top_include_dirs,
    ## scan_exclude or scan_exclude_rsync in /etc/mirrorbrain.conf.
    ##
    $contents =~ s{</(PRE|pre|table)>.*$}{}s;
    for my $line (split "\n", $contents) {
      $line =~ s/<\/*t[rd].*?>/ /g;
      print "$identifier: line: $line\n" if $verbose > 2;
      if($line =~ m{^(.*)[Hh][Rr][Ee][Ff]="([^"]+)">([^<]+)</[Aa]>\s+([\w\s:-]+)\s+(-|[\d\.]+[KMG]?)}) {
        my ($pre, $name1, $name2, $date, $size) = ($1, $2, $3, $4, $5);
        next if $name1 =~ m{^/} or $name1 =~ m{^\.\.};
        if($verbose > 2) {
          print "$identifier: pre $pre\n";
          print "$identifier: name1 $name1\n";
          print "$identifier: name2 $name2\n";
          print "$identifier: date $date\n";
          print "$identifier: size $size\n";
        }
        $name1 =~ s{%([\da-fA-F]{2})}{pack 'U', hex $1}ge;
        $name1 =~ s{^\./}{};
        my $dir = 1 if $pre =~ m{"\[DIR\]"};
        #print "$identifier: $pre^$name1^$date^$size\n" if $verbose > 1;
        my $t = length($name) ? "$name/$name1" : $name1;
        if($size eq '-' and ($dir or $name1 =~ m{/$})) {
          ## we must be really sure it is a directory, when we come here.
          ## otherwise, we'll retrieve the contents of a file!
          sleep($recursion_delay) if $recursion_delay;
          push @r, http_readdir($identifier, $id, $urlraw, $t, 0);
        }
        else {
          ## it is a file.
          my $time = str2time($date);
          my $len = byte_size($size);

          # str2time returns undef in some rare cases causing KILL! FIXME
          # workaround: don't store files with broken times
          if(not defined($time)) {
            print "$identifier: Error: str2time returns undef on parsing \"$date\". Skipping file $name1\n";
            print "$identifier: current line was:\n$line\nat url $url/$name\nname= $name1\n" if $verbose > 1;
          }
          elsif(largefile_check($identifier, $id, $t, $len)) {
            #save timestamp and file in database
            if(save_file($t, $identifier, $id, $time, $re)) {
              push @r, [ $t , $time ];
            }
          }
        }
      }
    }
    print "$identifier: committing http dir $name\n" if $verbose > 2;
  } elsif($contents =~ s{^.*<thead>.*>Name<.*<tbody>}{}s) {
    ##  _ _       _     _   _             _
    ## | (_) __ _| |__ | |_| |_ _ __   __| |
    ## | | |/ _` | '_ \| __| __| '_ \ / _` |
    ## | | | (_| | | | | |_| |_| |_) | (_| |
    ## |_|_|\__, |_| |_|\__|\__| .__/ \__,_|
    ##      |___/              |_|
    ## Oh look, it's a lighttpd directory index!
    $contents =~ s{</tbody>.*$}{}s;
    for my $line (split "\n", $contents) {
      $line =~ s/<\/*t[rd].*?>/ /g;
      print "$identifier: line: $line\n" if $verbose > 2;
      if($line =~ m{^(.*)[Hh][Rr][Ee][Ff]="([^"]+)">([^<]+)</[Aa]>.+([\w\s:-]+)\s+(-|[\d\.]+[KMG]?)}) {
        my ($pre, $name1, $name2, $date, $size) = ($1, $2, $3, $4, $5);
        next if $name1 =~ m{^/} or $name1 =~ m{^\.\.};
        if($verbose > 2) {
          print "$identifier: pre $pre\n";
          print "$identifier: name1 $name1\n";
          print "$identifier: name2 $name2\n";
          print "$identifier: date $date\n";
          print "$identifier: size $size\n";
        }
        $name1 =~ s{%([\da-fA-F]{2})}{pack 'U', hex $1}ge;
        $name1 =~ s{^\./}{};
        my $dir = 1 if $pre =~ m{>Directory<};
        my $t = length($name) ? "$name/$name1" : $name1;
        if($size eq '-' and ($dir or $name1 =~ m{/$})) {
          ## we must be really sure it is a directory, when we come here.
          ## otherwise, we'll retrieve the contents of a file!
          sleep($recursion_delay) if $recursion_delay;
          push @r, http_readdir($identifier, $id, $urlraw, $t, 0);
        }
        else {
          ## it is a file.
          my $time = $date;
          my $len = byte_size($size);

          # str2time returns undef in some rare cases causing KILL! FIXME
          # workaround: don't store files with broken times
          if(not defined($time)) {
            print "$identifier: Error: str2time returns undef on parsing \"$date\". Skipping file $name1\n";
            print "$identifier: current line was:\n$line\nat url $url/$name\nname= $name1\n" if $verbose > 1;
          }
          elsif(largefile_check($identifier, $id, $t, $len)) {
            #save timestamp and file in database
            if(save_file($t, $identifier, $id, $time, $re)) {
              push @r, [ $t , $time ];
            }
          }
        }
      }
    }
    print "$identifier: committing http dir $name\n" if $verbose > 2;
 } elsif($contents =~ s{^<html>.*<head><title>Index of .*<h1>Index of .*</h1><hr><pre><a href="../">../</a>}{}s) {
    ##              _
    ##  _ __   __ _(_)_ __ __  __
    ## | '_ \ / _` | | '_ \\ \/ /
    ## | | | | (_| | | | | |>  <
    ## |_| |_|\__, |_|_| |_/_/\_\
    ##        |___/
    ##
    ## Oh look, it's a nginx directory index!
    $contents =~ s{<pre><a href="../">../</a>.*</pre><hr></body>$}{}s;
    for my $line (split "\n", $contents) {
      #$line =~ s/<\/*t[rd].*?>/ /g;
      print "$identifier: line: $line\n" if $verbose > 2;

      # <a href="addons/">addons/</a>                                            14-May-2010 15:38                   -
      if($line =~ m{^<a href="([^"]+)">([^<]+)</a>\s*([\w\s:-]+)\s+(-|[\d\.]+)}) {

        my ($name1, $name2, $date, $size) = ($1, $2, $3, $4, $5);
        next if $name1 =~ m{^/} or $name1 =~ m{^\.\.};
        if($verbose > 2) {
          print "$identifier: name1 $name1\n";
          print "$identifier: name2 $name2\n";
          print "$identifier: date $date\n";
          print "$identifier: size $size\n";
        }
        #$name1 =~ s{%([\da-fA-F]{2})}{pack 'c', hex $1}ge;
        #$name1 =~ s{^\./}{};
        my $t = length($name) ? "$name/$name1" : $name1;
        if($size eq '-' and ($name1 =~ m{/$})) {
          ## we must be really sure it is a directory, when we come here.
          ## otherwise, we'll retrieve the contents of a file!
          sleep($recursion_delay) if $recursion_delay;
          push @r, http_readdir($identifier, $id, $urlraw, $t, 0);
        }
        else {
          ## it is a file.
          my $time = $date;
          my $len = byte_size($size);

          # str2time returns undef in some rare cases causing KILL! FIXME
          # workaround: don't store files with broken times
          if(not defined($time)) {
            print "$identifier: Error: str2time returns undef on parsing \"$date\". Skipping file $name1\n";
            print "$identifier: current line was:\n$line\nat url $url/$name\nname= $name1\n" if $verbose > 1;
          }
          elsif(largefile_check($identifier, $id, $t, $len)) {
            #save timestamp and file in database
            if(save_file($t, $identifier, $id, $time, $re)) {
              push @r, [ $t , $time ];
            }
          }
        }
      }
    }
    print "$identifier: finished http dir $name\n" if $verbose > 2;
 }
  else {
    ## we come here, whenever we stumble into an automatic index.html
    $contents = substr($contents, 0, 500);
    print "$identifier: unparseable HTML index in /$name\n" if $verbose;
    warn Dumper $contents, "$identifier: http_readdir: unknown HTML format" if $verbose > 1;
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
  die "byte_size: $len not impl\n";
}



#    _____ _____ ____
#   |  ___|_   _|  _ \
#   | |_    | | | |_) |
#   |  _|   | | |  __/
#   |_|     |_| |_|
#
# $file_count = scalar ftp_readdir($row->{identifier}, $row->{id}, $row->{baseurl_ftp}, $ftp_timer, $start_dir);
# first call: $ftp undefined
sub ftp_readdir
{
  my ($identifier, $id, $url, $ftp_timer, $name, $ftp) = @_;
  $ftp_timer_global = $ftp_timer;

  my $ftp_age = (time() - $ftp_timer_global);
  print "$identifier: last command issued $ftp_age"."s ago\n" if $verbose > 2;
  $ftp_timer_global = time();

  my $item;

  print "$identifier: ftp dir: $name\n" if $verbose > 1;

  my $urlraw = $url;
  my $re = ''; $re = $1 if $url =~ s{#(.*?)$}{};
  $url =~ s{/+$}{};	# we add our own trailing slashes...


  my $toplevel = ($ftp) ? 0 : 1;
  $ftp = ftp_connect($identifier, "$url/$name") unless defined $ftp;
  return unless defined $ftp;
  my $text = ftp_cont($ftp, "$url/$name");

  if(!ref($text) && $text =~ m/^(\d\d\d)\s/) {	# some FTP status code? Not good.

    # Bug: Net::FTP wrongly reports timeouts (421) as code 550:
    # sunsite.informatik.rwth-aachen.de: ftp dir: ftp://sunsite.informatik.rwth-aachen.de/pub/linux/opensuse/distribution/11.0/repo/debug/suse/i686
    # Net::FTP=GLOB(0x112f480)>>> CWD /pub/linux/opensuse/distribution/11.0/repo/debug/suse/i686
    # Net::FTP=GLOB(0x112f480)<<< 421 Timeout.
    # sunsite.informatik.rwth-aachen.de: ftp status code 550 (550 failed: ftp-cwd(/pub/linux/opensuse/distribution/11.0/repo/debug/suse/i686):  ), closing.
    #
    # Thus, if the connection is older than 60 seconds, we attempt a reconnect.
    # Otherwise we quit.
    if ($ftp_age > 60) {
      warn "$identifier: ftp status code $1. Last command " . $ftp_age . "s ago; attempting reconnect\n";
      print "$identifier: $text" if $verbose > 2;
      ftp_close($ftp);
      $ftp = ftp_connect($identifier, "$url/$name");
      return unless defined $ftp;
      $text = ftp_cont($ftp, "$url/$name");
    } else {
      warn "$identifier: ftp status code $1, closing.\n";
      print "$identifier: $text" if $verbose > 2;
      ftp_close($ftp);
      return;
    }
  }

  print "$identifier: ".join("\n", @$text)."\n" if $verbose > 2;

  my @r;
  for my $i (0..$#$text) {
    # -rw-r--r--    1 804      804        255436 Nov 09 03:13 The Big Picture.tar.gz
    if($text->[$i] =~ m/^([dl-])(.........).*\s(\d+)\s(\w\w\w\s+\d\d?\s+\d\d:?\d\d)\s+(\S+.*)$/) {
      my ($type, $mode, $size, $timestamp, $fname) = ($1, $2, $3, $4, $5);
      next if $fname eq "." or $fname eq "..";

      #print "$name / $fname\n";

      # are we looking at a top-level directory name?
      # (can be recognized by name being an empty string)
      if (!length($name) && scalar(@top_include_list)) {
        my $included = 0;
        foreach my $item(@top_include_list) {
          if ($fname =~ $item) {
            $included = 1;
          }
        }
        if (!$included) {
          print "$identifier: not in top_include_list: $fname\n" if $verbose > 1;
          next;
        }
      }

      my $excluded = 0;
      my $s = "$name/$fname";
      if($type eq "d") {
        $s = "$s/";
      }
      for $item(@exclude_list) {
        if ($s =~ $item) {
          print "$identifier: $s ignored (matches $item)\n" if $verbose > 0;
          $excluded = 1;
        }
      }
      next if ($excluded);

      #convert to timestamp
      my $time = str2time($timestamp);
      my $t = length($name) ? "$name/$fname" : $fname;

      if($type eq "d") {
        if($mode !~ m{r.[xs]r.[xs]r.[xs]}) {
          print "$identifier: bad mode $mode, skipping directory $fname\n" if $verbose;
          next;
        }
        sleep($recursion_delay) if $recursion_delay;
        push @r, ftp_readdir($identifier, $id, $urlraw, $ftp_timer_global, $t, $ftp);
      }

      if($type eq 'l') {
        print "$identifier: ignoring symlink ($t)\n" if $verbose > 1;
      } else {
        if ($mode !~ m{r..r..r..}) {
          print "$identifier: bad mode $mode, skipping file $fname\n" if $verbose;
          next;
        }
        #save timestamp and file in database
        if(largefile_check($identifier, $id, $t, $size)) {
          if(save_file($t, $identifier, $id, $time, $re)) {
            push @r, [ $t , $time ];
          }
        }
      }
    } else {
      if ($text->[$i] !~ /^total/) {
        print "$identifier: line could not be parsed: $text->[$i]\n";
      }
    }
  }

  print "$identifier: committing ftp dir $name\n" if $verbose > 2;

  ftp_close($ftp) if $toplevel;
  return @r;
}


sub save_file
{
  my ($path, $identifier, $serverid, $mod_re, $ign_re) = @_;

  #
  # optional patch the file names by adding or removing components.
  # you never know what strange paths mirror admins choose.
  #

  return undef if $ign_re and $path =~ m{$ign_re};

  if ($mod_re and $mod_re =~ m{@([^@]*)@([^@]*)}) {
    print "$identifier: save_file: $path + #$mod_re -> " if $verbose > 2;
    my ($m, $r) = ($1, $2);
    $path =~ s{$m}{$r};
    print "$path\n" if $verbose > 2;
  }

  $path =~ s{^/+}{};  # be sure we have no leading slashes.
  $path =~ s{//+}{/}g;  # avoid double slashes.

  # explicitely tell Perl that the filename is in UTF-8 encoding
  $path = decode_utf8($path);

  my $hash = sha256_hex($path);
  push @new_file_hashes, $hash unless delete $db_files_hash_id_map{$hash};

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
  $req->header('Accept' => '*/*');

  # Pass request to the user agent and get a response back
  my $res = $ua->request($req);

  # Check the outcome of the response
  if ($res->is_success) {
    return (1,$res->content);
  }
  else {
    return (0,$res->status_line);
  }
}

# callback function
sub rsync_cb
{
  my ($priv, $name, $len, $mode, $mtime) = @_;
  return 0 if $name eq '.' or $name eq '..';
  my $r = 0;

  if($priv->{subdir}) {
    # subdir is expected not to start or end in slashes.
    $name = $priv->{subdir} . '/' . $name;

  }


  if($mode & 0x1000) {        # directories have 0 here.
    if($mode & 004) { # readable for the world is good.
      # params for largefile check: url=$ary_ref->{$priv->{serverid}}/$name, size=$len
      if(largefile_check($priv->{identifier}, $priv->{serverid}, $name, $len) == 0) {
        printf "$priv->{identifier}: warning: $name cannot be delivered via HTTP! Skipping\n" if $verbose > 0;
      }
      else {
        $name = save_file($name, $priv->{identifier}, $priv->{serverid}, $mtime, $priv->{re});
        $priv->{counter}++;
        if (($priv->{counter} % 50) == 0) {
          print "$priv->{identifier}: commit after 50 files\n" if $verbose > 2;
        }

        $r = [$name, $len, $mode, $mtime];
        printf "%s: rsync ADD: %03o %12.0f %-25s %-50s\n", $priv->{identifier}, ($mode & 0777), $len, scalar(localtime $mtime), $name if $verbose > 2;
      }
    }
    else {
      printf "%s: rsync skip: %03o %12.0f %-25s %-50s\n", $priv->{identifier}, ($mode & 0777), $len, scalar(localtime $mtime), $name if $verbose > 1;
    }
  }
  elsif($mode == 0755) {
    printf "%s: rsync dir: %03o %12.0f %-25s %-50s\n", $priv->{identifier}, ($mode & 0777), $len, scalar(localtime $mtime), $name if $verbose > 1;
  }
  elsif($mode == 020777) {
    printf "%s: rsync link: %03o %12.0f %-25s %-50s\n", $priv->{identifier}, ($mode & 0777), $len, scalar(localtime $mtime), $name if $verbose > 2;
  }
  return $r;
}



# example rsync address:
#  rsync://user:passwd@ftp.sunet.se/pub/Linux/distributions/opensuse/#@^opensuse/@@
# parameters:
#  serverid: id field content from database row
#  url: base url from database
#  d: base directory (can be 'undef'): parameter to the '-d' switch
sub rsync_readdir
{
  my ($identifier, $serverid, $url, $d) = @_;
  return 0 unless $url;

  $url =~ s{^rsync://}{}s; # trailing s: treat as single line, strip off protocol id
  my $re = ''; $re = $1 if $url =~ s{#(.*?)$}{}; # after a hash can be a regexp, see example above
  my $cred = $1 if $url =~ s{^(.*?)@}{}; # username/passwd if specified
  die "$identifier: rsync_readdir: cannot parse url '$url'\n" unless $url =~ m{^([^:/]+)(:(\d*))?(.*)$};
  my ($host, $dummy, $port, $path) = ($1,$2,$3,$4);
  $port = 873 unless $port;
  $path =~ s{^/+}{};

  my $peer = { identifier => $identifier, addr => inet_aton($host), port => $port, serverid => $serverid };
  $peer->{re} = $re if $re;
  $peer->{pass} = $1 if $cred and $cred =~ s{:(.*)}{};
  $peer->{user} = $cred if $cred;
  $peer->{subdir} = $d if length $d;
  $peer->{counter} = 0;
  $path .= "/". $d if length $d;
  eval{
    rsync_get_filelist($identifier, $peer, $path, 0, \&rsync_cb, $peer, 1)
  };
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
sub sread
{
  local *SS = shift;
  my $len = shift;
  my $ret = '';
  while($len > 0) {
    alarm 600;
    my $r = sysread(SS, $ret, $len, length($ret));
    alarm 0;
    die("read error") unless $r;
    $len -= $r;
    die("read too much") if $r < 0;
  }
  return $ret;
}



sub swrite
{
  local *SS = shift;
  my ($var, $len) = @_;
  $len = length($var) unless defined $len;
  return if $len == (syswrite(SS, $var, $len) || 0);
  warn "syswrite: $!\n";
}



sub muxread
{
  my $identifier = shift;
  local *SS = shift;
  my $len = shift;

  #print "$identifier: muxread $len\n";
  while(length($rsync_muxbuf) < $len) {
    #print "$identifier: muxbuf len now ".length($muxbuf)."\n";
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
      warn("$identifier: tag=8 $msg\n") if $tag == 8;
      print "$identifier: info: $msg\n";
      next;
    }
    warn("$identifier: unknown tag: $tag\n");
    return undef;
  }
  my $ret = substr($rsync_muxbuf, 0, $len);
  $rsync_muxbuf = substr($rsync_muxbuf, $len);
  return $ret;
}



sub rsync_get_filelist
{
  my ($identifier, $peer, $syncroot, $norecurse, $callback, $priv, $sorted) = @_;
  my $syncaddr = $peer->{addr};
  my $syncport = $peer->{port};

  $SIG{ALRM} = sub { $verbose++; $verbose++; die localtime(time) . " $identifier: rsync timeout...\n" };

  if(!defined($peer->{have_md4})) {
    ## why not rely on %INC here?
    $peer->{have_md4} = 0;
    eval {
      # this causes funny messages, if perl-Digest-MD4 is not installed:
      # __DIE__: (/usr/bin/scanner 311 main::rsync_readdir => /usr/bin/scanner 961 main::rsync_get_filelist => /usr/bin/scanner 1046 (eval))
      # not sure whether it is worth installing it.
      # we never had it on mirrordb.opensuse.org, the main openSUSE scan host.
      require Digest::MD4;
      $peer->{have_md4} = 1;
    };
  }
  $syncroot =~ s/^\/+//;
  my $module = $syncroot;
  $module =~ s/\/.*//;
  my $tcpproto = getprotobyname('tcp');
  socket(S, PF_INET, SOCK_STREAM, $tcpproto) || die("$identifier: socket: $!\n");
  setsockopt(S, SOL_SOCKET, SO_KEEPALIVE, pack("l",1));
  connect(S, sockaddr_in($syncport, $syncaddr)) || die("$identifier: connect: $!\n");
  my $hello = "\@RSYNCD: 28\n";
  swrite(*S, $hello);
  my $buf = '';
  alarm 600;
  sysread(S, $buf, 4096);
  alarm 0;
  die("$identifier: protocol error [$buf]\n") if $buf !~ /^\@RSYNCD: ([\d.]+)\n/s;
  $peer->{rsync_protocol} = $1;
  $peer->{rsync_protocol} = 28 if $peer->{rsync_protocol} > 28;
  swrite(*S, "$module\n");
  while(1) {
    alarm 600;
    sysread(S, $buf, 4096);
    alarm 0;
    die("$identifier: protocol error [$buf]\n") if $buf !~ s/\n//s;
    last if $buf eq "\@RSYNCD: OK";
    die("$identifier: $buf\n") if $buf =~ /^\@ERROR/s;
    if($buf =~ /^\@RSYNCD: AUTHREQD /) {
      die("$identifier: '$module' needs authentification, but Digest::MD4 is not installed\n") unless $peer->{have_md4};
      my ($user,$password)='';
      # my $user = "nobody"; is not needed IMO
      $user = $peer->{user} if defined $peer->{user};
      $password = $peer->{pass} if defined $peer->{pass};
      my $digest = "$user ".Digest::MD4::md4_base64("\0\0\0\0$password".substr($buf, 18))."\n";
      swrite(*S, $digest);
      next;
    }
  }
  my @args = ('--server', '--sender', '-rl');
  push @args, '--exclude=/*/*' if $norecurse;

  if(@top_include_list && !defined($peer->{subdir})) {
    foreach my $item (@top_include_list) {
      push @args, "--include=/$item";
    }
    push @args, "--exclude=/*";
  }

  print "$identifier: rsync excludes: @exclude_list_rsync\n" if $verbose > 1;
  foreach my $item (@exclude_list_rsync) {
    push @args, "--exclude=$item";
  }
  print "$identifier: rsync args: @args\n" if $verbose > 2;

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
    $flags = muxread($identifier, *S, 1);
    $flags = ord($flags);
    # printf "flags = %02x\n", $flags;
    last if $flags == 0;
    $flags |= ord(muxread($identifier, *S, 1)) << 8 if $peer->{rsync_protocol} >= 28 && ($flags & 0x04) != 0;
    my $l1 = $flags & 0x20 ? ord(muxread($identifier, *S, 1)) : 0;
    my $l2 = $flags & 0x40 ? unpack('V', muxread($identifier, *S, 4)) : ord(muxread($identifier, *S, 1));
    $name = substr($name, 0, $l1).muxread($identifier, *S, $l2);
    my $len = unpack('V', muxread($identifier, *S, 4));
    if($len == 0xffffffff) {
      $len = unpack('V', muxread($identifier, *S, 4));
      my $len2 = unpack('V', muxread($identifier, *S, 4));
      $len += $len2 * 4294967296;
    }
    $mtime = unpack('V', muxread($identifier, *S, 4)) unless $flags & 0x80;
    $mode = unpack('V', muxread($identifier, *S, 4)) unless $flags & 0x02;
    my $mmode = $mode & 07777;
    if(($mode & 0170000) == 0100000) {
      $mmode |= 0x1000;
    } elsif (($mode & 0170000) == 0040000) {
      $mmode |= 0x0000;
    } elsif (($mode & 0170000) == 0120000) {
      $mmode |= 0x2000;
      muxread($identifier, *S, unpack('V', muxread($identifier, *S, 4)));
    } else {
      print "$name: unknown mode: $mode\n";
      next;
    }
    # sort and process buffer when folder changes
    if ($callback && $sorted && !($mmode & 0x1000)) {
        for my $file (sort {$a->[0] cmp $b->[0]} @filelist) {
            &$callback($priv, $file->[0], $file->[1], $file->[2], $file->[3], $file->[4]);
        }
        @filelist = ();
    }
    if(!$sorted && $callback) {
      &$callback($priv, $name, $len, $mmode, $mtime);
    }
    else {
      push @filelist, [$name, $len, $mmode, $mtime];
    }
  }
  my $io_error = unpack('V', muxread($identifier, *S, 4));

  # rsync_send_fin
  swrite(*S, pack('V', -1));      # switch to phase 2
  swrite(*S, pack('V', -1));      # switch to phase 3
  if($peer->{rsync_protocol} >= 24) {
    swrite(*S, pack('V', -1));    # goodbye
  }
  close(S);
  if ($callback && $sorted) {
    # sort and process remaining buffer
    for my $file (sort {$a->[0] cmp $b->[0]} @filelist) {
      &$callback($priv, $file->[0], $file->[1], $file->[2], $file->[3]);
    }
  }
  return undef if $callback;
  return @filelist unless $sorted;
  return sort {$a->[0] cmp $b->[0]} @filelist;
}



sub ftp_connect
{
  my ($identifier, $url) = @_;

  if($url =~ s{^(\w+)://}{}) {	# no protocol prefix please
    if(lc $1 ne 'ftp') {
      warn "$identifier: ftp_connect: not an ftp url: '$1://$url'\n";
      return undef;
    }
  }
  $url =~ s{/.*$}{};  # no path components please
  my $port = 21;
  $port = $1 if $url =~ s{:(\d+)$}{};	# port number?

  my $user = 'anonymous';
  my $pass = "$0@" . Net::Domain::hostfqdn;
  my $auth = $1 if $url =~ s{^([^:]*:[^@]*)@}{};	# auth data?
  if (defined $auth) {
    $user = $1 if $auth =~ s{^([^:]*):}{};
    $pass = $auth;
  }

  my $ftp = Net::FTP->new($url, Timeout => 360, Port => $port, Debug => (($verbose||0)>2)?1:0, Passive => 1, Hash => 0);
  unless (defined $ftp) {
    warn "$identifier: ftp_connect($identifier, $url, $port) failed: $! $@\n";
    return undef;
  }
  $ftp->login($user, $pass) or warn "$identifier: ftp-login failed: $! $@\n";
  $ftp->type('I');		# binary mode please.
  print STDERR "$identifier: connected to $url, ($user,$pass)\n" if $verbose > 1;
  return $ftp;
}



sub ftp_close
{
  my ($ftp) = @_;
  $ftp->quit;
}



sub ftp_cont
{
  my ($ftp, $path) = @_;
  $path =~ s{^\w+://([^@]+@)?[^/:]+(:\d+)?/}{/};	# no proto/auth/host/port/prefix, please.
  $ftp->cwd($path) or return "550 failed: ftp-cwd($path): $! $@";

  $ftp->dir();
  # In an array context, returns a list of lines returned from the server.
  # In a scalar context, returns a reference to a list.
  #
  ## should use File::Listing to parse this
  #
  # [
  #   'drwx-wx-wt    2 incoming 49           4096 Jul 03 23:00 incoming',
  #   '-rw-r--r--    1 root     root     16146417 Jul 04 23:12 ls-Ral.txt'
  # ],
}



# double check large files.
# some mirrors can't deliver large files via http.
# try a http range request for files larger than 2G/4G in http/ftp/rsync
sub largefile_check
{
  my ($identifier, $id, $path, $size, $recurse) = @_;

  if(not defined $recurse) {
    $recurse = 0;
  }
  # don't follow more than three redirections
  return if($recurse >= 3);

  $http_size_hint = 128;
  $http_slice_counter = 2*$http_size_hint;

  if($size==0) {
    if($path =~ m{.*\.iso$}) {
      print "$identifier: Error: cd size is zero! Illegal file $path\n";
      goto error;
    }
  }

  goto all_ok if($size <= $gig2);

  my $url = "$ary_ref->{$id}->{baseurl}/$path";
  my $header = new HTTP::Headers('Range' => "bytes=".($gig2-$http_size_hint)."-".($gig2+1));
  my $req = new HTTP::Request('GET', "$url", $header);

  #turn off implicit redirects (handle manually):
  $ua->max_redirect(0);

  my $result = $ua->request(
    $req,
    sub {
      my ($chunk, $result) = @_;
      $http_slice_counter -= $http_size_hint;
      die() if $http_slice_counter <= 0;
      return $chunk;
    },
    $http_size_hint
  );

  my $code = $result->code();
  goto all_ok if($code == 206 or $code == 200);
  # check some redirect types:
  # 301 - permanent redirect -> client is adviesd to remember redirected address
  # 302 - temporary redirect -> client shall continue using this address
  # 303 - redirect from POST command to another URI via GET command
  # 307 - same as 302 except different caching behaviour
  if($code == 301 or $code == 302 or $code == 303 or $code == 307) {
    if($result->header('location') =~ m{^ftp:.*}) {
      print localtime(time) . " $identifier: Moved to ftp location, assuming success if followed\n" if $verbose >= 1;
      goto all_ok;
    }
    if($result->header('location') =~ m{^http:.*}) {
      print localtime(time) . " $identifier: [RECURSE] Moved to other http location, recursing scan...\n" if $verbose >= 1;
      return largefile_check($id, $result->header('location'), $size, $recurse+1);
    }
  }

  if($result->code() == 416) {
    print localtime(time) . " $identifier: Error: range error: filesize broken for file $url\n" if $verbose >= 1;
  }
  else {
    print localtime(time) . " $identifier: Error ".$result->code()." occured\n" if $verbose >= 1;
  }

  error:
  return 0;

  all_ok:
  return 1;
}

# vim: ai ts=2 sw=2 smarttab expandtab
