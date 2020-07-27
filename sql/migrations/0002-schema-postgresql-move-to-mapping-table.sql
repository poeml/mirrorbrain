DROP TABLE IF EXISTS files, server_files CASCADE;

CREATE TABLE files
(
    id bigint GENERATED ALWAYS AS IDENTITY,
    path character varying NOT NULL,
    path_hash bytea GENERATED ALWAYS AS (sha256((path)::bytea)) STORED,
    mtime timestamp with time zone,
    size bigint,
    md5 bytea,
    sha1 bytea,
    sha256 bytea,
    sha1piecesize integer,
    sha1pieces bytea,
    btih bytea,
    pgp text,
    zblocksize integer,
    zhashlens character varying(8),
    zsums bytea,
    created_at timestamp(6) with time zone NOT NULL DEFAULT now(),
    updated_at timestamp(6) with time zone NOT NULL DEFAULT now(),
    CONSTRAINT pk_files PRIMARY KEY (id)
);

CREATE INDEX idx_files_on_mtime_and_size
    ON files USING btree
    (mtime ASC NULLS LAST, size ASC NULLS LAST);

CREATE INDEX idx_files_on_created_at
    ON files USING btree
    (created_at ASC NULLS LAST);

CREATE UNIQUE INDEX idx_files_on_path_unique
    ON files USING btree
    (path ASC NULLS LAST);

CREATE INDEX idx_files_path_hash ON files(encode(path_hash,'hex'));

CREATE TABLE server_files
(
    server_id smallint NOT NULL,
    file_id integer NOT NULL,
    CONSTRAINT fk_server_files_files FOREIGN KEY (file_id)
        REFERENCES files (id) MATCH SIMPLE
        ON UPDATE NO ACTION
        ON DELETE CASCADE,
    CONSTRAINT fk_server_files_servers FOREIGN KEY (server_id)
        REFERENCES server (id) MATCH SIMPLE
        ON UPDATE NO ACTION
        ON DELETE CASCADE
);

CREATE UNIQUE INDEX idx_server_files_on_server_id_and_file_id
    ON server_files USING btree
    (file_id ASC NULLS LAST, server_id ASC NULLS LAST);

CREATE INDEX idx_server_files_server_id
    ON server_files USING btree
    (server_id ASC NULLS LAST);

CREATE MATERIALIZED VIEW files_mirror_count AS
  SELECT
      files.id AS file_id,
      count(server_files.file_id) AS count
    FROM files
    LEFT JOIN server_files
      ON files.id = server_files.file_id
    WHERE files.created_at < (now()-'3 months'::interval)
    GROUP BY files.id
    ORDER BY files.id
WITH DATA;

CREATE INDEX idx_files_mirror_count_id
  ON files_mirror_count
  USING btree(file_id);

CREATE INDEX idx_files_mirror_count_nonzero_count
  ON files_mirror_count
  USING btree(count)
  WHERE count > 0;

CREATE INDEX idx_files_mirror_count_zero_count
  ON files_mirror_count
  USING btree(count)
  WHERE count = 0;

CREATE OR REPLACE FUNCTION mb_cleanup_old_files()
    RETURNS integer
    LANGUAGE 'plpgsql'

AS $BODY$
  DECLARE
   return_data integer;
BEGIN
  WITH affected_rows AS (
    -- DELETE FROM files
    --     USING files AS f
    --     LEFT OUTER JOIN server_files AS sf
    --     ON  sf.files_id = f.id
    --   WHERE sf.files_id = f.id
    --     AND sf.server_id IS NULL
    --     AND files.mtime < (now()-'3 months'::interval)
    DELETE FROM files
      WHERE id IN (
        SELECT file_id
          FROM files_mirror_count
          WHERE count = 0
    )
    RETURNING *
  )
  SELECT INTO return_data count(*)
    FROM affected_rows;
  RETURN return_data;
END $BODY$;

-- FUNCTION: public.mirr_add_byid(integer, integer)

DROP FUNCTION IF EXISTS public.mirr_add_byid(integer, integer);

CREATE OR REPLACE FUNCTION public.mb_mirror_add_file(
    arg_serverid integer,
    arg_fileid integer
  )
    RETURNS integer
    LANGUAGE 'plpgsql'
AS $BODY$
DECLARE
   return_data integer;
BEGIN
  WITH affected_rows AS (
    INSERT INTO server_files (file_id, server_id)
      VALUES (arg_fileid, arg_serverid)
      ON CONFLICT DO NOTHING
      RETURNING *
  )
  SELECT INTO return_data count(*)
    FROM affected_rows;
  RETURN return_data;
END;
$BODY$;

-- FUNCTION: public.mb_add_bypath(integer, text)

DROP FUNCTION IF EXISTS public.mirr_add_bypath(integer, text);

CREATE OR REPLACE FUNCTION public.mb_mirror_add_file(
    arg_serverid integer,
    arg_path text
  )
    RETURNS integer
    LANGUAGE 'plpgsql'
AS $BODY$
DECLARE
    fileid integer;
    return_data integer;
BEGIN
    SELECT INTO fileid id FROM files WHERE path = arg_path;

    -- There are three cases to handle, and we want to handle each of them
    -- with the minimal effort.
    -- In any case, we return a file id in the end.
    IF fileid IS NULL THEN
        RAISE DEBUG 'we do not know about the file "%".', arg_path;
    ELSE
        RAISE DEBUG 'update existing file entry (path: % id: %)', arg_path, fileid;
        SELECT into return_data mb_mirror_add_file(arg_serverid, fileid);
        RETURN return_data;
    END IF;

    RETURN 0;
END;
$BODY$;

-- FUNCTION: public.mirr_del_byid(integer, integer)

DROP FUNCTION IF EXISTS public.mirr_del_byid(integer, integer);

--
-- TODO: for consistency with the mb_mirror_add_file() function se should probably also add
--       mb_mirror_remove_file(integer, text)
CREATE OR REPLACE FUNCTION public.mb_mirror_remove_file(
    arg_serverid integer,
    arg_fileid integer
  )
    RETURNS integer
    LANGUAGE 'plpgsql'
AS $BODY$
DECLARE
   return_data integer;
BEGIN
  WITH affected_rows AS (
    DELETE FROM server_files WHERE server_id = arg_serverid AND file_id = arg_fileid RETURNING *
  )
  SELECT INTO return_data count(*)
    FROM affected_rows;
  RETURN return_data;
END;
$BODY$;


-- FUNCTION: public.mirr_get_name(integer)

DROP FUNCTION IF EXISTS public.mirr_get_name(integer);

CREATE OR REPLACE FUNCTION public.mb_mirror_identifier(
    arg_serverid integer
  )
    RETURNS text
    LANGUAGE 'sql'
AS $BODY$
  SELECT identifier FROM server WHERE id=arg_serverid
$BODY$;

-- FUNCTION: public.mirr_get_name(smallint[])

DROP FUNCTION IF EXISTS public.mirr_get_name(smallint[]);

CREATE OR REPLACE FUNCTION public.mb_mirror_identifier(
    ids smallint[]
  )
    RETURNS text[]
    LANGUAGE 'sql'
AS $BODY$
  SELECT array_agg(identifier::text) FROM server WHERE id = ANY(ids);
$BODY$;

-- FUNCTION: public.mirr_get_nfiles(integer)

DROP FUNCTION IF EXISTS public.mirr_get_nfiles(integer);

CREATE OR REPLACE FUNCTION public.mb_mirror_filecount(
	  arg_serverid integer
  )
    RETURNS bigint
    LANGUAGE 'sql'
AS $BODY$
  SELECT count(*) FROM server_files WHERE server_id = arg_serverid;
$BODY$;

-- FUNCTION: public.mirr_get_nfiles(text)

DROP FUNCTION IF EXISTS public.mirr_get_nfiles(text);

CREATE OR REPLACE FUNCTION public.mb_mirror_filecount(
	  arg_server_identifier text
  )
    RETURNS bigint
    LANGUAGE 'sql'
AS $BODY$
  SELECT count(*) FROM server_files WHERE (SELECT id FROM server WHERE identifier = arg_server_identifier) = server_id;
$BODY$;

-- FUNCTION: public.mirr_hasfile_byid(integer, integer)

DROP FUNCTION IF EXISTS public.mirr_hasfile_byid(integer, integer);

CREATE OR REPLACE FUNCTION public.mb_mirror_has_file(
    arg_serverid integer,
    arg_fileid integer
  )
    RETURNS boolean
    LANGUAGE 'plpgsql'
AS $BODY$
DECLARE
    result integer;
BEGIN
    SELECT INTO result 1 FROM server_files WHERE file_id = arg_fileid AND server_id = arg_serverid;
    IF result > 0 THEN
        RETURN true;
    END IF;
    RETURN false;
END;
$BODY$;

-- FUNCTION: public.mirr_hasfile_byname(integer, text)

DROP FUNCTION IF EXISTS public.mirr_hasfile_byname(integer, text);

CREATE OR REPLACE FUNCTION public.mb_mirror_has_file(
    arg_serverid integer,
    arg_path text
  )
    RETURNS boolean
    LANGUAGE 'plpgsql'
AS $BODY$
DECLARE
    result integer;
    fileid integer;
BEGIN
    SELECT INTO fileid id FROM files WHERE path = arg_path;

    -- There are three cases to handle, and we want to handle each of them
    -- with the minimal effort.
    -- In any case, we return a file id in the end.
    IF fileid IS NULL THEN
        RAISE DEBUG 'we do not know about the file "%".', arg_path;
    ELSE
      SELECT INTO result 1 FROM server_files WHERE file_id = fileid AND server_id = arg_serverid;
      IF result > 0 THEN
          RETURN true;
      END IF;
    END IF;
    RETURN false;
END;
$BODY$;

DROP VIEW IF EXISTS hexhash;
CREATE VIEW hexhash AS
  SELECT
    files.*,
    encode(md5,        'hex') AS md5hex,
    encode(sha1,       'hex') AS sha1hex,
    encode(sha256,     'hex') AS sha256hex,
    encode(sha1pieces, 'hex') AS sha1pieceshex,
    encode(btih,       'hex') AS btihhex,
    encode(zsums,      'hex') AS zsumshex
  FROM files;

ALTER INDEX IF EXISTS country_pkey                            RENAME TO pk_country;
ALTER INDEX IF EXISTS hash_pkey                               RENAME TO pk_hash;
ALTER INDEX IF EXISTS marker_pkey                             RENAME TO pk_marker;
ALTER INDEX IF EXISTS region_pkey                             RENAME TO pk_region;
ALTER INDEX IF EXISTS server_pkey                             RENAME TO pk_server;
ALTER INDEX IF EXISTS version_pkey                            RENAME TO pk_version;

ALTER INDEX IF EXISTS server_enabled_status_baseurl_score_key RENAME TO idx_server_enabled_status_baseurl_score;
ALTER INDEX IF EXISTS server_identifier_key                   RENAME TO idx_server_identifier_unique;

DROP TABLE IF EXISTS pfx2asn;