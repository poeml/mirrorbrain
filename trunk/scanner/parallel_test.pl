#! /usr/bin/perl -w
#
# par_test.pl -- poor man's multithreading test.
# (c) 2007 Novell inc., jw@suse.de.
# Distribute under GPLv2 if it proves worthy.
#
# Fixed number of worker processs.
# Next is forked when one dies. No real ipc here.

use Data::Dumper;

if (scalar @ARGV)
  {
    for my $i (@ARGV)
      {
        print "$$: sleep $i ...\n";
	sleep $i;
	print "$$: \t ... $i done.\n";
      }
    exit 0;
  }

my @n = qw(12 3 34 3 5 6 8 1 7 5);
my $w = 4;
my @w;
my %where;

my $loop = 1;

do
  {
    for my $n (@n)
      {
	my $skip = 0;
	map { $skip++ if $_->{id} == $n } @w;
	if ($skip)
	  {
	    sleep 1;
	    print "skipping $n\n";
	    next;
	  }

	# check if one of the workers is idle
	my $i = wait_worker(\@w, $w);
	$w[$i] = { id => $n, pid => fork_child($i, $n) };
	my $l = join(',', map { sprintf "%3d", $_->{id} } @w);
	print "\t\t\t\t$l\n";
      }
  }
while ($loop);

while (wait > -1)
  {
    ;		# reap all children.
  }

exit 0;
####################################

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
	  my $id = $a->[$i]{id};
	  unless (kill(0, $p)) 		# already dead? okay take him home.
	    {
	      print "kill(0, $p) returned 0. reusing $i!\n";
	      undef $a->[$i];	
	      return $i;
	    }
	  $pids{$p} = $i;		# not? okay wait.
	}
      my $p = wait;
      if (defined(my $i = $pids{$p}))
        {
	  print "worker $i, pid=$p exit: $?\n";
	  undef $a->[$i];
	  return $i;			# now, been there, done that.
	}
      # $p = -1 or other silly things...
      warn "wait failed: $!, $?\n";
    }
}

sub fork_child
{
  my ($idx, @data) = @_;
  if (my $p = fork())
    {
      # parent 
      print "worker $idx, ($data[0]) pid=$p start.\n";
      return $p;
    }
  exec { $0 } "scanner [#$idx]", @data;	# ourselves with a false name and some data.
}
