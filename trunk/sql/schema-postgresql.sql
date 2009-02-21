
-- 
-- MirrorBrain Database scheme for PostgreSQL
-- 

-- --------------------------------------------------------
BEGIN;
-- --------------------------------------------------------


CREATE TABLE "file" (
        "id" serial NOT NULL PRIMARY KEY,
        "path" varchar(512) NOT NULL
);

CREATE INDEX "file_path_key" ON "file" ("path");

-- --------------------------------------------------------

CREATE TABLE "server" (
        "id" serial NOT NULL PRIMARY KEY,
        "identifier" varchar(64) NOT NULL UNIQUE,
        "baseurl"       varchar(128) NOT NULL,
        "baseurl_ftp"   varchar(128) NOT NULL,
        "baseurl_rsync" varchar(128) NOT NULL,
        "enabled"        boolean NOT NULL,
        "status_baseurl" boolean NOT NULL,
        "region"  varchar(2) NOT NULL,
        "country" varchar(2) NOT NULL,
        "asn" integer NOT NULL,
        "prefix" varchar(18) NOT NULL,
        "score" smallint NOT NULL,
        "scan_fpm" integer NOT NULL,
        "last_scan" timestamp with time zone NULL,
        "comment" text NOT NULL,
        "operator_name" varchar(128) NOT NULL,
        "operator_url" varchar(128) NOT NULL,
        "public_notes" varchar(512) NOT NULL,
        "admin"       varchar(128) NOT NULL,
        "admin_email" varchar(128) NOT NULL,
        "lat" numeric(6, 3) NULL,
        "lng" numeric(6, 3) NULL,
        "country_only" boolean NOT NULL,
        "region_only" boolean NOT NULL,
        "as_only" boolean NOT NULL,
        "prefix_only" boolean NOT NULL,
        "other_countries" varchar(512) NOT NULL,
        "file_maxsize" integer NOT NULL default 0
);

CREATE INDEX "server_enabled_status_baseurl_score_key" ON "server" (
        "enabled", "status_baseurl", "score"
);

-- --------------------------------------------------------

CREATE TABLE "file_server" (
        "serverid" integer NOT NULL REFERENCES "server" ("id") DEFERRABLE INITIALLY DEFERRED,
        "fileid" integer NOT NULL REFERENCES "file" ("id") DEFERRABLE INITIALLY DEFERRED,
	-- we actually never used the timestamp_file column.
        -- "timestamp_file" timestamp with time zone NULL,
        -- and the next one should be a unix epoch, which needs only 4 bytes instead of 8:
        "timestamp_scanner" timestamp with time zone NULL,
        UNIQUE ("fileid", "serverid")
);
-- NOTICE:  CREATE TABLE / UNIQUE will create implicit index "file_server_fileid_key" for table "file_server"
-- CREATE TABLE

CREATE INDEX "file_server_serverid_fileid_key" ON "file_server" ("serverid", "fileid");


-- For ORM's that require a primary key named 'id'; this way we don't need to
-- actually store it and have an index for it.
-- the index alone needs 800MB for 1.000.000 files
CREATE VIEW file_server_withpk AS 
        SELECT '1' 
	|| LPAD(CAST(fileid AS TEXT), 12, '0') 
	|| LPAD(CAST(serverid AS TEXT), 12, '0') 
	AS id, serverid, fileid, timestamp_scanner 
	FROM file_server;

-- --------------------------------------------------------

CREATE TABLE "marker" (
        "id" serial NOT NULL PRIMARY KEY,
        "subtree_name" varchar(128) NOT NULL,
        "markers" varchar(512) NOT NULL
);

-- --------------------------------------------------------

CREATE TABLE "country" (
        "id" serial NOT NULL PRIMARY KEY,
        "code" varchar(2) NOT NULL,
        "name" varchar(64) NOT NULL
);

CREATE TABLE "region" (
    "id" serial NOT NULL PRIMARY KEY,
    "code" varchar(2) NOT NULL,
    "name" varchar(64) NOT NULL
);



-- --------------------------------------------------------

--
-- from_unixtime
--
-- Takes a seconds-since-the-epoch integer and returns a timestamp
--
-- => select from_unixtime('1233609530');
--     from_unixtime    
-- ---------------------
--  2009-02-02 22:18:50
-- (1 row)

CREATE OR REPLACE FUNCTION from_unixtime(integer) RETURNS timestamp AS '
        SELECT $1::abstime::timestamp without time zone AS result
' LANGUAGE 'SQL';

-- --------------------------------------------------------
COMMIT;
-- --------------------------------------------------------

