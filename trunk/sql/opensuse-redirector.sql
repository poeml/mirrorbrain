
-- 
-- Database: `redirector`
-- 

-- --------------------------------------------------------

-- 
-- Table structure of table `file`
-- 

CREATE TABLE `file` (
  `id` int(11) unsigned NOT NULL auto_increment,
  `path` varchar(512) NOT NULL,
  PRIMARY KEY  (`id`),
  KEY `file_path_idx` (`path`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1 ;

-- --------------------------------------------------------

-- 
-- Table structure of table `file_server`
-- 

CREATE TABLE `file_server` (
  `fileid` int(11) unsigned NOT NULL default '0',
  `serverid` int(11) unsigned NOT NULL default '0',
  `timestamp_file` timestamp NOT NULL default CURRENT_TIMESTAMP on update CURRENT_TIMESTAMP,
  `timestamp_scanner` timestamp NOT NULL default '0000-00-00 00:00:00',
  `path_md5` binary(22) NOT NULL,
  KEY `fileid` (`fileid`,`serverid`),
  KEY `file_server_fileid_idx` (`fileid`),
  KEY `file_server_serverid_idx` (`serverid`),
  KEY `file_server_fileid_serverid_idx` (`fileid`,`serverid`),
  KEY `file_server_path_md5_serverid_idx` (`path_md5`,`serverid`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1 ;

-- --------------------------------------------------------

-- 
-- Table structure of table `server`
-- 

CREATE TABLE `server` (
  `id` int(11) unsigned NOT NULL auto_increment,
  `identifier` varchar(64) NOT NULL,
  `baseurl` varchar(128) NOT NULL,
  `baseurl_ftp` varchar(128) NOT NULL,
  `enabled` tinyint(1) NOT NULL,
  `status_baseurl` tinyint(1) NOT NULL,
  `last_scan` timestamp NULL default NULL,
  `region` varchar(10) default NULL,
  `country` char(2) default NULL,
  `score` int(11) default NULL,
  `scan_fpm` int(11) default NULL,
  `baseurl_rsync` varchar(128) default NULL,
  `comment` text,
  `admin_email` text,
  `netblock` text,
  `admin` text,
  `lat` float default NULL,
  `lng` float default NULL,
  `country_only` tinyint(1) default '0',
  `region_only` tinyint(1) default '0',
  PRIMARY KEY  (`id`),
  KEY `server_enabled_status_baseurl_score_idx` (`enabled`,`status_baseurl`,`score`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;
