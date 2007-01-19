#!/usr/bin/perl -w
#
# (C) 2007 jw@suse.de, Novell Inc.
# Distribute under GPLv2.
#
#
# torture.pl - explore the limits of the database query performance.
#
# Usage: 
#   torture.pl suse/i586/java-1_5_0-sun-1.5.0_update8-19.i586.rpm suse/setup/descr/packages.en
# (to look up a few files)
# or 
#   torture.pl
# (to run an infinte benchmark without random files.)

use DBI;
use strict;
use Data::Dumper;
use Time::HiRes;
use Digest::MD5;

my $howmany = 10;	## an initial value, will increment...
my $verbose = 1;
my $log_sql;
my $file_count = 0;

my $db = { name=>'dbi:mysql:dbname=redirector;host=galerkin.suse.de', user=>'root', pass=>"" };
# my $db = { name=>'dbi:mysql:dbname=redirector', user=>'root', pass=>"" };

$db->{dbh} = DBI->connect( $db->{name}, $db->{user} || '',  $db->{pass} || '',
  {
    RaiseError => 0, 
    PrintError => 1, 
    ShowErrorStatement => 1
  });

if (@ARGV)
  {
    my $server = table_fetch($db, 'server', [qw[id identifier baseurl baseurl_ftp last_scan enabled status_baseurl status_baseurl_ftp]]);
    my $active = fixup_server($server);

    while (my $file = shift)
      {
	my ($found,$time) = redirect($db, $file, $active);
	my @ids = keys %$found;
	die "file '$file' not found.\n" unless @ids;

	for my $id (@ids)
	  {
	    my $m = $server->{rows}{$id};
	    printf "%s: file_t=%s scan_t=%s\n",
	      $m->{identifier}, $found->{$id}{timestamp_file}, $found->{$id}{timestamp_scanner};
	    print "$m->{baseurl_ftp}/$file\n" if defined $m->{baseurl_ftp} and $m->{status_baseurl_ftp};
	    print "$m->{baseurl}/$file\n"     if defined $m->{baseurl}     and $m->{status_baseurl};
	  }
	printf "query_t=%.2f msec.\n\n", $time * 1000.0;
        # die Dumper $found, $server;
      }
    exit 0;
  }

for (;;)
  {
    my $files = table_fetch($db, 'file', [qw[path]], '', 'ARRAY');
    my @files = map { $_->{path} } @{$files->{rows}};

#    for my $file (@{$files->{rows}})
#      {
#	my $md5 = Digest::MD5::md5_base64($file->{path});
#	my $stmt = "UPDATE file_server SET path_md5 = '$md5' where fileid = $file->{id}";
#	$db->{dbh}->do($stmt) or die $db->{dbh}->errstr;
#      }
#exit;

    my $server = table_fetch($db, 'server', [qw[id enabled status_baseurl status_baseurl_ftp]]);
    my $active = fixup_server($server);

    my $start = Time::HiRes::time();
    if (!@files)
      {
	warn "empty table: redirector.file\n";
        sleep 10;
	next;
      }
    my $new_file_count = scalar @files;
    print "$new_file_count files known.\n" if $new_file_count != $file_count;
    $file_count = scalar @files;
    for my $i (1..$howmany)
      {
	my $file = $files[int(rand scalar @files)];
	redirect($db, $file, $active);
      }
    my $end = Time::HiRes::time();
    printf "$howmany redirects, %8.3f sec, -> %9.1f redirects/second\n", $end-$start, $howmany/($end-$start);
    $howmany *= 10 if $end-$start < 2.0;
  }

exit 0;
########################################################################

sub fixup_server
{
  my ($table) = @_;
  my @r;

  for my $s (values %{$table->{rows}})
    {
      for my $field qw(baseurl baseurl_ftp)
	{
	  $s->{$field} =~ s{/+$}{} if defined $s->{$field};
	}
      $s->{active} = $s->{enabled} && ($s->{status_baseurl_ftp} || $s->{status_baseurl});
      push @r, $s->{id} if $s->{active};
    }
  return \@r;
}

sub get_id
{
  my ($dbi, $path) = @_;
  my $where = 'WHERE path = '.$db->{dbh}->quote($path); 
  my $r = table_fetch($db, 'file', [qw[id]], $where, 'ARRAY');
  return $r->{rows}[0]{id};
}

sub redirect
{
  my ($dbi, $path, $active) = @_;

## split in two queries or use a subquery
#  my $id = get_id($dbi, $path);
#  my $where = 'WHERE fileid = '.$db->{dbh}->quote($id); 

## further optimization: prepare the statement, and send only the path parameter down the wire
#
#  my $where = 'WHERE fileid = (SELECT id FROM file WHERE path = ' . $db->{dbh}->quote($path) . ' LIMIT 1)';
#  $where .= ' AND serverid in ('. join(',',@$active).')' if $active;

  my $where = "WHERE path_md5 = '" . Digest::MD5::md5_base64($path) . "'";

  my $r = table_fetch($db, 'file_server', [qw[serverid timestamp_file timestamp_scanner]], $where);
  return $r->{rows}, $r->{time};
}

##
## first column must be DISTINCT for table_fetch, as it returns a hash.
## $where and $distinct are optional and may contain verbatim SQL syntax.
## looks like DISTINCT is always implicit, right?
## $dbi->{table}{$table} is implicitly set, when $distinct is undef.
##
sub table_fetch
{
  my ($dbi, $table, $cols, $where, $distinct) = @_;

  my $method = 'hashref';
  $method = 'arrayref' if defined($distinct) and $distinct =~ s{\s*ARRAY\s*$}{};
  $distinct = undef    if defined($distinct) and $distinct =~ m{^\s*$};

  my $stmt = 'SELECT';
  $stmt .= " " . $distinct if defined $distinct;
  $stmt .= ' ' . join(',',@$cols) . " from $table";
  $stmt .= " " . $where if defined $where;
#  db_connect($dbi, $stmt);
  my $keyname = $cols->[0];
  if ($keyname =~ m{\sas\s+(\w+)}i)
    {
      $keyname = $1; 	# allow for column aliasing!
      warn "table_fetch using alias keyname '$keyname' in selectall_hashref('$stmt', '$keyname');\n" 
        if $method eq 'hashref' and $verbose > 1;

      ## Strange sometimes selectall_hashref works with aliases, sometimes it does not.
      ## yes: table_fetch($db, 'lic_pac', [q{packname||'|'||version||'|'||filename||'|'||lic_match_id||'|'||line_no as id},'line_count']);
      ## no: table_fetch_a($db, 'lic_pac', ['EXTRACT(EPOCH FROM MAX(tstamp)) AS u']);
    }

  ## no need to sort when returning hashref
  $stmt .= " ORDER BY $keyname" if $method eq 'arrayref' and $stmt !~ m{\bORDER BY\b[^']*$}i;

  $stmt =~ s{\b(LIMIT \d+)\b([^\)]*$)}{ $2 $1}si;	# move LIMIT clause always at the end, if any.
  						# protect subselects, by not crossing ')'!

  print "selectall_hashref '$stmt'\n" if $verbose > 3;	
  # SELECT packname||'|'||version||'|'||filename||'|'||lic_match_id||'|'||line_no as id,line_count from lic_pac WHERE lic_match_id > 0 and packname = 'nessus-core''
  # causes 
  # Use of uninitialized value in hash element at /usr/lib/perl5/vendor_perl/5.8.1/x86_64-linux-thread-multi/DBI.pm line 1751.
  # when one of the fields IS NULL.
  #

  $log_sql->($stmt) if $log_sql;
  my $select_start = Time::HiRes::time();
  my $rows;
  if ($method eq 'arrayref')
    {
      my $a = $dbi->{dbh}->selectall_arrayref($stmt);
      for my $r (@$a)
        {
	  my %r;
	  for my $i (0..$#$cols)
	    {
	      my $c = $cols->[$i];
	      $c =~ s{^.*\s+as\s+}{};
	      $r{$c} = $r->[$i];
	    }
	  push @$rows, \%r;
	}
    }
  else
    {
      $rows = $dbi->{dbh}->selectall_hashref($stmt, $keyname);
    }
  print "selectall_$method done\n" if $verbose > 3;
  $log_sql->('') if $log_sql;

  my $duration = Time::HiRes::time()-$select_start;
  if (0.3 <= $duration)
    {
      printf "table_fetch %.2fs: %s;\n", $duration, $stmt if $verbose > 1;
    }

  if ($dbi->{dbh}->err)
    {
      $dbi->{dbh}->disconnect;
      die "cannot read table $table from $dbi->{name}: $stmt: " . $dbi->{dbh}->errstr;
    }

  my $r = { rows => $rows, name => $table, cols => $cols, query => $stmt, time => $duration };
  return $r if defined $distinct;

  delete $dbi->{table}{$table} if $dbi->{table}{$table};	# help avoid mem-leak?
  $dbi->{table}{$table} = $r;
}

