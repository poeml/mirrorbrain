CREATE DATABASE IF NOT EXISTS `redirector`;

USE `redirector`;

CREATE TABLE `files` (
  `id` int(11) UNSIGNED NOT NULL AUTO_INCREMENT,
  `pathhash` varchar(32) NOT NULL,
  `path` tinytext NOT NULL,
  `timestamp` timestamp(14) NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY(`id`),
  INDEX `pathhash`(`pathhash`)
)
ENGINE=MYISAM
ROW_FORMAT=dynamic;

CREATE TABLE `servers` (
  `id` int(11) UNSIGNED NOT NULL AUTO_INCREMENT,
  `identifier` varchar(64) NOT NULL,
  `baseurl` varchar(128) NOT NULL,
  `baseurl_ftp` varchar(128) NOT NULL,
  `status` enum('online','offline','disabled') DEFAULT 'online',
  PRIMARY KEY(`id`)
)
ENGINE=MYISAM
ROW_FORMAT=dynamic;

CREATE TABLE `country_region` (
  `country` char(2) NOT NULL,
  `regionid` int(11) UNSIGNED NOT NULL DEFAULT '0',
  PRIMARY KEY(`country`)
)
ENGINE=MYISAM
ROW_FORMAT=fixed;

CREATE TABLE `region_server` (
  `regionid` tinyint(4) UNSIGNED NOT NULL DEFAULT '0',
  `serverid` int(11) NOT NULL DEFAULT '0',
  `score` int(11) NOT NULL DEFAULT '0',
  INDEX `regionid`(`regionid`, `serverid`, `score`)
)
ENGINE=MYISAM
ROW_FORMAT=fixed;

CREATE TABLE `country_server` (
  `country` char(2) NOT NULL,
  `serverid` int(11) UNSIGNED NOT NULL DEFAULT '0',
  `score` int(11) UNSIGNED NOT NULL DEFAULT '0'
)
ENGINE=MYISAM
ROW_FORMAT=fixed;

CREATE TABLE `file_server` (
  `fileid` int(11) UNSIGNED NOT NULL DEFAULT '0',
  `serverid` int(11) UNSIGNED NOT NULL DEFAULT '0',
  INDEX `fileid`(`fileid`, `serverid`)
)
ENGINE=MYISAM
ROW_FORMAT=fixed;
