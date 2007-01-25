-- 
-- Tabellenstruktur für Tabelle `redirect_stats`
-- 

CREATE TABLE `redirect_stats` (
  `id` int(11) NOT NULL auto_increment,
  `project` varchar(255) default NULL,
  `repository` varchar(255) default NULL,
  `arch` varchar(10) default NULL,
  `filename` varchar(255) default NULL,
  `filetype` varchar(10) default NULL,
  `version` varchar(255) default NULL,
  `release` varchar(255) default NULL,
  `count` int(11) default '0',
  `package` varchar(255) default NULL,
  `created_at` timestamp NULL default CURRENT_TIMESTAMP,
  `counted_at` timestamp NULL default NULL,
  PRIMARY KEY  (`id`),
  KEY `project` (`project`),
  KEY `package` (`package`),
  KEY `repository` (`repository`),
  KEY `arch` (`arch`),
  KEY `filename` (`filename`),
  KEY `filetype` (`filetype`),
  KEY `version` (`version`),
  KEY `release` (`release`)
) ENGINE=MyISAM AUTO_INCREMENT=1 ;
