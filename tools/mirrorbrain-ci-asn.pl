#!/usr/bin/env perl

use strict;
use warnings;
use feature qw( say );

use MaxMind::DB::Writer::Tree;

my $filename = 'mirrorbrain-ci-asn.mmdb';

# Your top level data structure will always be a map (hash).  The MMDB format
# is strongly typed.  Describe your data types here.
# See https://metacpan.org/pod/MaxMind::DB::Writer::Tree#DATA-TYPES

my %types = (
  'autonomous_system_number'       => 'uint64',
  'autonomous_system_organization' => 'utf8_string',
  'prefix_len'                     => 'uint16',
);

my $tree = MaxMind::DB::Writer::Tree->new(

    # "database_type" is some arbitrary string describing the database.  At
    # MaxMind we use strings like 'GeoIP2-City', 'GeoIP2-Country', etc.
    database_type => 'GeoLite2-ASN',

    languages             => ['en'],

    # "description" is a hashref where the keys are language names and the
    # values are descriptions of the database in that language.
    description =>
        { en => 'MirrorBrain CI ASN DB' },

    # "ip_version" can be either 4 or 6
    ip_version => 6,

    # add a callback to validate data going in to the database
    map_key_type_callback => sub { $types{ $_[0] } },

    # "record_size" is the record size in bits.  Either 24, 28 or 32.
    record_size => 24,

    remove_reserved_networks => 0,
);

# {'autonomous_system_number': 3680, 'autonomous_system_organization': 'Novell, Inc.', 'ip_address': '130.57.72.10', 'prefix_len': 20}
$tree->insert_network(
  '127.0.0.1/32',
  {
    'autonomous_system_number' => 1231,
    'autonomous_system_organization' => 'ACME Master Inc',
    'prefix_len' => 32,
  },
);

$tree->insert_network(
  '127.0.0.2/32',
  {
    'autonomous_system_number' => 1232,
    'autonomous_system_organization' => 'ACME Inc',
    'prefix_len' => 32, 
  },
);

$tree->insert_network(
  '127.0.0.3/32',
  {
    'autonomous_system_number' => 1233,
    'autonomous_system_organization' => 'ACME GmbH',
    'prefix_len' => 32, 
  },
);

$tree->insert_network(
  '127.0.0.4/32',
  {
    'autonomous_system_number' => 1234,
    'autonomous_system_organization' => 'ACME Asia Inc',
    'prefix_len' => 32, 
  },
);

# Write the database to disk.
open my $fh, '>:raw', $filename;
$tree->write_tree( $fh );
close $fh;

say "$filename has now been created";
