
-- 
-- MirrorBrain Database scheme for MySQL
-- 

-- --------------------------------------------------------

CREATE TABLE `file` (
  `id` int(11) unsigned NOT NULL auto_increment,
  `path` varchar(512) NOT NULL,
  PRIMARY KEY  (`id`),
  KEY `file_path_idx` (`path`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1 ;

-- --------------------------------------------------------

CREATE TABLE `file_server` (
  `fileid` int(11) unsigned NOT NULL default '0',
  `serverid` int(11) unsigned NOT NULL default '0',
-- we never used the timestamp_file column
-- the default and update trigger were not used either
-- `timestamp_file` timestamp NOT NULL default CURRENT_TIMESTAMP on update CURRENT_TIMESTAMP,
  `timestamp_scanner` timestamp NOT NULL default '0000-00-00 00:00:00',
  KEY `file_server_fileid_idx` (`fileid`),
  KEY `file_server_serverid_idx` (`serverid`),
  KEY `file_server_fileid_serverid_idx` (`fileid`,`serverid`),
) ENGINE=InnoDB DEFAULT CHARSET=latin1 ;

-- --------------------------------------------------------

CREATE TABLE `server` (
  `id` int(11) unsigned NOT NULL auto_increment,
  `identifier` varchar(64) NOT NULL,
  `baseurl` varchar(128) NOT NULL,
  `baseurl_ftp` varchar(128) NOT NULL,
-- inconsistently, baseurl_rsync can be null - we should get rid of this.
  `baseurl_rsync` varchar(128) default NULL,
  `enabled` tinyint(1) NOT NULL default '0',
  `status_baseurl` tinyint(1) NOT NULL default '0',
-- region should be altered to char(2) - as we use it
  `region` varchar(10) default NULL,
  `country` char(2) default NULL,
  `asn` int(11) default NULL,
  `prefix` char(18) default NULL,
  `score` int(11) default NULL,
  `scan_fpm` int(11) default NULL,
  `last_scan` timestamp NULL default NULL,
  `comment` text,
  `admin` text,
  `admin_email` text,
  `operator_name` text NOT NULL,
  `operator_url` text NOT NULL,
  `public_notes` varchar(512) NOT NULL default '',
  `lat` float default NULL,
  `lng` float default NULL,
  `country_only` tinyint(1) default '0',
  `region_only` tinyint(1) default '0',
  `as_only` tinyint(1) default '0',
  `prefix_only` tinyint(1) default '0',
  `other_countries` text NOT NULL,
  `file_maxsize` int(11) NOT NULL default '0',
  PRIMARY KEY  (`id`),
  UNIQUE KEY `identifier` (`identifier`),
  KEY `server_enabled_status_baseurl_score_idx` (`enabled`,`status_baseurl`,`score`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;


CREATE TABLE `marker` (
  `id` int(11) unsigned NOT NULL auto_increment,
  `subtree_name` varchar(128) NOT NULL,
  `markers` varchar(512) NOT NULL,
  PRIMARY KEY  (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;


CREATE TABLE `country` (
  `id` tinyint(1) NOT NULL auto_increment,
  `code` char(2) NOT NULL,
  `name` char(64) NOT NULL,
  PRIMARY KEY  (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


CREATE TABLE `region` (
  `id` int(2) NOT NULL auto_increment,
  `code` char(2) NOT NULL,
  `name` char(64) NOT NULL,
  PRIMARY KEY  (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 | 

