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
	print "$$: \t ... done.\n";
      }
    exit 0;
  }

my @n = qw(12 3 34 5 6 23 8 11 9 2 4 1 7 10 3 15 1);
my $w = 4;
my @w;
for my $n (@n)
  {
    # check if one of the workers is idle
    my $i = wait_worker(\@w, $w);
    $w[$i]{pid} = fork_child($i, $n);
  }

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
      print "worker $idx, pid=$p start.\n";
      return $p;
    }
  exec { $0 } "scanner [#$idx]", @data;	# ourselves with a false name and some data.
}
