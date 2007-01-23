
-- phpMyAdmin SQL Dump
-- version 2.9.1.1
-- http://www.phpmyadmin.net
-- 
-- Host: localhost
-- Erstellungszeit: 19. Januar 2007 um 14:12
-- Server Version: 5.0.26
-- PHP-Version: 5.2.0
-- 
-- Datenbank: `redirector`
-- 

-- --------------------------------------------------------

-- 
-- Tabellenstruktur für Tabelle `country_region`
-- 

CREATE TABLE `country_region` (
`country` char(2) NOT NULL,
`regionid` int(11) unsigned NOT NULL default '0',
PRIMARY KEY  (`country`)
) ENGINE=MyISAM DEFAULT CHARSET=latin1 ROW_FORMAT=FIXED;

-- --------------------------------------------------------

-- 
-- Tabellenstruktur für Tabelle `country_server`
-- 

CREATE TABLE `country_server` (
`country` char(2) NOT NULL,
`serverid` int(11) unsigned NOT NULL default '0',
`score` int(11) unsigned NOT NULL default '0'
) ENGINE=MyISAM DEFAULT CHARSET=latin1 ROW_FORMAT=FIXED;

-- --------------------------------------------------------

-- 
-- Tabellenstruktur für Tabelle `file`
-- 

CREATE TABLE `file` (
`id` int(11) unsigned NOT NULL auto_increment,
`path` varchar(512) NOT NULL,
PRIMARY KEY  (`id`)
) ENGINE=MyISAM  DEFAULT CHARSET=latin1 ROW_FORMAT=DYNAMIC AUTO_INCREMENT=84461 ;

-- --------------------------------------------------------

-- 
-- Tabellenstruktur für Tabelle `file_server`
-- 

CREATE TABLE `file_server` (
`fileid` int(11) unsigned NOT NULL default '0',
`serverid` int(11) unsigned NOT NULL default '0',
`timestamp_file` timestamp NOT NULL default CURRENT_TIMESTAMP on update CURRENT_TIMESTAMP,
`timestamp_scanner` timestamp NOT NULL default '0000-00-00 00:00:00',
KEY `fileid` (`fileid`,`serverid`)
) ENGINE=MyISAM DEFAULT CHARSET=latin1 ROW_FORMAT=FIXED;

-- --------------------------------------------------------

-- 
-- Tabellenstruktur für Tabelle `region_server`
-- 

CREATE TABLE `region_server` (
`regionid` tinyint(4) unsigned NOT NULL default '0',
`serverid` int(11) NOT NULL default '0',
`score` int(11) NOT NULL default '0',
KEY `regionid` (`regionid`,`serverid`,`score`)
) ENGINE=MyISAM DEFAULT CHARSET=latin1 ROW_FORMAT=FIXED;

-- --------------------------------------------------------

-- 
-- Tabellenstruktur für Tabelle `server`
-- 

CREATE TABLE `server` (
`id` int(11) unsigned NOT NULL auto_increment,
`identifier` varchar(64) NOT NULL,
`baseurl` varchar(128) NOT NULL,
`baseurl_ftp` varchar(128) NOT NULL,
`enabled` tinyint(1) NOT NULL,
`status_baseurl` tinyint(1) NOT NULL,
`status_baseurl_ftp` tinyint(1) NOT NULL,
`status_ping` tinyint(1) NOT NULL,
`last_scan` timestamp NULL default NULL,
`country` char(2),
`region` varchar(10),
`score` tinyint(1),
PRIMARY KEY  (`id`)
) ENGINE=MyISAM  DEFAULT CHARSET=latin1 ROW_FORMAT=DYNAMIC AUTO_INCREMENT=9 ;
