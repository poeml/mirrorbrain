#! /usr/bin/perl -w
#
# parse_config.pl -- convert from config/mirrors.region.ini to mysql insert statements.
#
# (C) 2007, jw@suse.de, Novell Inc.
# Distribute under GPLv2
#
# FIXME: This parser just dumps all info into the server table.
#        I've added region country and score columns there to allow such a simple import.


# use Data::Dumper;
my %m;

for my $ini qw(americas africa asia europe)
  {
    my $key = '';
    my $file =    "mirrors.$ini.ini";
    open IN, "<", $file or die "open $file failed: $!";
    while (defined(my $line = <IN>))
      {
        if ($line =~ m{^\[(\S+)\]})
	  {
	    $key = $1;
	    $m{$key}{region} = $ini;
	    $m{$key}{country} = $1 if $key =~ m{\.(\w\w)$};
	    next;
	  }
	if ($line =~ m{^(\w+)\s*=\s*(\S*)})
	  {
	    $m{$key}{$1} = $2;
	  }
      }
    close IN;
  }

for my $i (sort keys %m)
  {
    my $m = $m{$i};

    map { $m->{$_}||='' } qw(baseurl_ftp baseurl region country);
    $m->{disable}||='false';
    $m->{score}||=0;

    my $e     = ($m->{disable} eq 'false') ? 1 : 0;
    my $s_b   = ($m->{baseurl}     and $e) ? 1 : 0;
    my $s_b_f = ($m->{baseurl_ftp} and $e) ? 1 : 0;
    print "INSERT INTO server (enabled, status_baseurl, status_baseurl_ftp, score, identifier, region, country, baseurl, baseurl_ftp)" .
    	" VALUES ($e, $s_b, $s_b_f, $m->{score}, '$i', '$m->{region}', '$m->{country}', '$m->{baseurl}', '$m->{baseurl_ftp}');\n";
  }
