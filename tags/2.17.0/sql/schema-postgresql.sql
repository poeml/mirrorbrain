
-- 
-- MirrorBrain Database scheme for PostgreSQL
-- 

-- before PL/pgSQL functions can be used, the languages needs to be "installed"
-- in the database. This is done with:
-- 
-- createlang plpgsql <dbname>

-- --------------------------------------------------------
BEGIN;
-- --------------------------------------------------------


CREATE TABLE "version" (
        "component" text NOT NULL PRIMARY KEY,
        "major" INTEGER NOT NULL,
        "minor" INTEGER NOT NULL,
        "patchlevel" INTEGER NOT NULL
);

-- --------------------------------------------------------


CREATE TABLE "filearr" (
        "id" serial NOT NULL PRIMARY KEY,
        "path" varchar(512) UNIQUE NOT NULL,
        "mirrors" smallint[]
);

-- --------------------------------------------------------


CREATE TABLE "hash" (
        "file_id" INTEGER REFERENCES filearr PRIMARY KEY,
        "mtime" INTEGER NOT NULL,
        "size" BIGINT NOT NULL,
        "md5"    BYTEA NOT NULL,
        "sha1"   BYTEA NOT NULL,
        "sha256" BYTEA NOT NULL,
        "sha1piecesize" INTEGER NOT NULL,
        "sha1pieces" BYTEA NOT NULL,
        "btih"   BYTEA NOT NULL,
        "pgp" TEXT NOT NULL,
        "zblocksize" SMALLINT NOT NULL,
        "zhashlens" VARCHAR(8),
        "zsums" BYTEA NOT NULL
);

-- For conveniency, this view provides the binary columns from the "hash" table
-- also encoded in hex
--
-- Note on binary data (bytea) column.
-- PostgreSQL escapes binary (bytea) data on output. But hex encoding is more
-- efficient (it results in shorter strings, and thus less data to transfer
-- over the wire, and it's also faster). The escape format doesn't make sense
-- for a new application (which we are).
-- On the other hand, storage in bytea is as compact as it can be, which is good.
-- The hex encoding function in PostgreSQL seems to be fast.
CREATE VIEW hexhash AS 
  SELECT file_id, mtime, size, 
         md5,
         encode(md5, 'hex') AS md5hex, 
         sha1,
         encode(sha1, 'hex') AS sha1hex, 
         sha256,
         encode(sha256, 'hex') AS sha256hex, 
         sha1piecesize, 
         sha1pieces,
         encode(sha1pieces, 'hex') AS sha1pieceshex,
         btih,
         encode(btih, 'hex') AS btihhex, 
         pgp,
         zblocksize,
         zhashlens,
         zsums,
         encode(zsums, 'hex') AS zsumshex
  FROM hash;

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





-- add a mirror to the list of mirrors where a file was seen
CREATE OR REPLACE FUNCTION mirr_add_byid(arg_serverid integer, arg_fileid integer) RETURNS integer AS $$
DECLARE
    arr smallint[];
BEGIN
    SELECT INTO arr mirrors FROM filearr WHERE id = arg_fileid;
    IF arg_serverid = ANY(arr) THEN
        RAISE DEBUG 'already there -- nothing to do';
        RETURN 0;
    ELSE
        arr := array_append(arr, arg_serverid::smallint);
        RAISE DEBUG 'arr: %', arr;
        update filearr set mirrors = arr where id = arg_fileid;
        return 1;
    END IF;
END;
$$ LANGUAGE 'plpgsql';


-- remove a mirror from the list of mirrors where a file was seen
CREATE OR REPLACE FUNCTION mirr_del_byid(arg_serverid integer, arg_fileid integer) RETURNS integer AS $$
DECLARE
    arr smallint[];
BEGIN
    SELECT INTO arr mirrors FROM filearr WHERE id = arg_fileid;

    IF NOT arg_serverid = ANY(arr) THEN
        -- it's not there - nothing to do
        RAISE DEBUG 'not there -- nothing to do';
        RETURN 0;
    ELSE
        arr := ARRAY(
                    SELECT arr[i] 
                    FROM generate_series(array_lower(arr, 1), array_upper(arr, 1)) 
                    AS i 
                    WHERE arr[i] <> arg_serverid
                );
        RAISE DEBUG 'arr: %', arr;
        -- update the array in the table
        -- if arr is empty, we could actually remove the row instead, thus deleting the file
        UPDATE filearr 
            SET mirrors = arr WHERE id = arg_fileid;
        RETURN 1;
    END IF;
END;
$$ LANGUAGE 'plpgsql';


-- check whether a given mirror is known to have a file (id)
CREATE OR REPLACE FUNCTION mirr_hasfile_byid(arg_serverid integer, arg_fileid integer) RETURNS boolean AS $$
DECLARE
    result integer;
BEGIN
    SELECT INTO result 1 FROM filearr WHERE id = arg_fileid AND arg_serverid = ANY(mirrors);
    IF result > 0 THEN
        RETURN true;
    END IF;
    RETURN false;
END;
$$ LANGUAGE 'plpgsql';

-- check whether a given mirror is known to have a file (name)
CREATE OR REPLACE FUNCTION mirr_hasfile_byname(arg_serverid integer, arg_path text) RETURNS boolean AS $$
DECLARE
    result integer;
BEGIN
    SELECT INTO result 1 FROM filearr WHERE path = arg_path AND arg_serverid = ANY(mirrors);
    IF result > 0 THEN
        RETURN true;
    END IF;
    RETURN false;
END;
$$ LANGUAGE 'plpgsql';


CREATE OR REPLACE FUNCTION mirr_add_bypath(arg_serverid integer, arg_path text) RETURNS integer AS $$
DECLARE
    fileid integer;
    arr smallint[];
BEGIN
    SELECT INTO fileid, arr
        id, mirrors FROM filearr WHERE path = arg_path;

    -- There are three cases to handle, and we want to handle each of them
    -- with the minimal effort.
    -- In any case, we return a file id in the end.
    IF arg_serverid = ANY(arr) THEN
        RAISE DEBUG 'nothing to do';
    ELSIF fileid IS NULL THEN
        RAISE DEBUG 'creating entry for new file.';
        INSERT INTO filearr (path, mirrors) VALUES (arg_path, ARRAY[arg_serverid]);
        fileid := currval('filearr_id_seq');
    ELSE
        RAISE DEBUG 'update existing file entry (id: %)', fileid;
        arr := array_append(arr, arg_serverid::smallint);
        update filearr set mirrors = arr where id = fileid;
    END IF;

    RETURN fileid;
EXCEPTION
    WHEN unique_violation THEN
        RAISE NOTICE 'file % was just inserted by somebody else', arg_path;
        -- just update it by calling ourselves again
        SELECT into fileid mirr_add_bypath(arg_serverid, arg_path);
        RETURN fileid;
END;
$$ LANGUAGE 'plpgsql';



CREATE OR REPLACE FUNCTION mirr_get_name(integer) RETURNS text AS '
    SELECT identifier FROM server WHERE id=$1
' LANGUAGE 'SQL';


CREATE OR REPLACE FUNCTION mirr_get_name(ids smallint[]) RETURNS text[] AS $$
DECLARE
    names text[];
    -- i integer;
BEGIN
    names := ARRAY(
                  select mirr_get_name(cast(ids[i] AS integer)) from generate_series(array_lower(ids, 1), array_upper(ids, 1)) as i
                  );
    RETURN names;
END;
$$ LANGUAGE 'plpgsql';


CREATE OR REPLACE FUNCTION mirr_get_nfiles(integer) RETURNS bigint AS '
    SELECT count(*) FROM filearr WHERE $1 = ANY(mirrors)
' LANGUAGE 'SQL';

CREATE OR REPLACE FUNCTION mirr_get_nfiles(text) RETURNS bigint AS '
    SELECT count(*) FROM filearr WHERE (SELECT id from server where identifier = $1) = ANY(mirrors)
' LANGUAGE 'SQL';


-- --------------------------------------------------------
COMMIT;
-- --------------------------------------------------------

-- vim: ft=sql ai ts=4 sw=4 smarttab expandtab
