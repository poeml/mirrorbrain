#!/usr/bin/env perl

use strict;
use warnings;
use feature qw( say );

use MaxMind::DB::Writer::Tree;

my $filename = 'mirrorbrain-ci-city.mmdb';

# Your top level data structure will always be a map (hash).  The MMDB format
# is strongly typed.  Describe your data types here.
# See https://metacpan.org/pod/MaxMind::DB::Writer::Tree#DATA-TYPES

my %types = (
  'city'      => 'map',
  'country'   => 'map',
  'continent' => 'map',
  'names'     => 'map',
  'location'  => 'map',
  'en'        => 'utf8_string',
  'code'      => 'utf8_string',
  'iso_code'  => 'utf8_string',
  'accuracy_radius' => 'uint16',
  'latitude' => 'float',
  'longitude' => 'float',
  'time_zone' => 'utf8_string',
);

my $tree = MaxMind::DB::Writer::Tree->new(

    # "database_type" is some arbitrary string describing the database.  At
    # MaxMind we use strings like 'GeoIP2-City', 'GeoIP2-Country', etc.
    database_type => 'GeoLite2-City',

    languages             => ['en'],

    # "description" is a hashref where the keys are language names and the
    # values are descriptions of the database in that language.
    description =>
        { en => 'MirrorBrain CI City DB' },

    # "ip_version" can be either 4 or 6
    ip_version => 6,

    # add a callback to validate data going in to the database
    map_key_type_callback => sub { $types{ $_[0] } },

    # "record_size" is the record size in bits.  Either 24, 28 or 32.
    record_size => 24,

    remove_reserved_networks => 0,
);

$tree->insert_network(
  '127.0.0.1/32',
  {
    location => {
      'accuracy_radius' => 1000,
      'latitude' => 37.751,
      'longitude' => -97.822,
      'time_zone' => 'America/Chicago'
    },
    city =>  {
      'names' => { 'en' => 'Provo' },
    },
    country =>  {
      'iso_code' => 'US',
      'names' => { 'en' => 'United States' },
    },
    continent => {
      'code'=> 'NA',
      'names'=> {
        'en'=> 'North America',
      }
    },
  },
);

$tree->insert_network(
  '127.0.0.2/32',
  {
    location => {
      'accuracy_radius' => 1000,
      'latitude' => 37.751,
      'longitude' => -97.822,
      'time_zone' => 'America/Chicago'
    },
    city =>  {
      'names' => { 'en' => 'Provo' },
    },
    country =>  {
      'iso_code' => 'US',
      'names' => { 'en' => 'United States' },
    },
    continent => {
      'code'=> 'NA',
      'names'=> {
        'en'=> 'North America',
      }
    },
  },
);

$tree->insert_network(
  '127.0.0.3/32',
  {
    'location' => { 
      'accuracy_radius' => 200,
      'latitude' => 49.4167,
      'longitude' => 8.7,
      'time_zone' => 'Europe/Berlin'
    },
    city =>  {
      'names' => { 'en' => 'Nuremberg' },
    },
    country =>  {
      'iso_code' => 'DE',
      'names' => { 'en' => 'Germany' },
    },
    continent => {
      'code'=> 'EU',
      'names'=> {
        'en'=> 'Europe',
      }
    },
  },
);

$tree->insert_network(
  '127.0.0.4/32',
  {
    location => {
      'accuracy_radius' => 1000,
      'latitude' => 34.7725,
      'longitude' => 113.7266,
      'time_zone' => 'Asia/Shanghai'
    },
    city =>  {
      'names' => { 'en' => 'Beijing' },
    },
    country =>  {
      'iso_code' => 'CN',
      'names' => { 'en' => 'China' },
    },
    continent => {
      'code'=> 'AS',
      'names'=> {
        'en'=> 'Asia',
      }
    },
  },
);

# Write the database to disk.
open my $fh, '>:raw', $filename;
$tree->write_tree( $fh );
close $fh;

say "$filename has now been created";
