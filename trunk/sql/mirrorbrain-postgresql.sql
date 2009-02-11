
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
        -- can we just omit the id column?
        -- no, sqlobject needs a primary key.
        "id" serial NOT NULL PRIMARY KEY,
        "serverid" integer NOT NULL REFERENCES "server" ("id") DEFERRABLE INITIALLY DEFERRED,
        "fileid" integer NOT NULL REFERENCES "file" ("id") DEFERRABLE INITIALLY DEFERRED,
        "timestamp_file" timestamp with time zone NULL,
        "timestamp_scanner" timestamp with time zone NULL,
        UNIQUE ("fileid", "serverid")
);

-- indexes that are created with "id" column:
-- NOTICE:  CREATE TABLE will create implicit sequence "file_server_id_seq" for serial column "file_server.id"
-- NOTICE:  CREATE TABLE / PRIMARY KEY will create implicit index "file_server_pkey" for table "file_server"
-- NOTICE:  CREATE TABLE / UNIQUE will create implicit index "file_server_fileid_key" for table "file_server"
-- CREATE TABLE

-- that are created when not using the "id" column:
-- NOTICE:  CREATE TABLE / UNIQUE will create implicit index "file_server_fileid_key" for table "file_server"
-- CREATE TABLE

CREATE INDEX "file_server_serverid_key" ON "file_server" ("serverid");
CREATE INDEX "file_server_fileid_serverid_key" ON "file_server" ("fileid", "serverid");

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

