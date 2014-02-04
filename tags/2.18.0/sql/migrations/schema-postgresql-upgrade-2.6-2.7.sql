
CREATE TABLE "filearr" (
        "id" serial NOT NULL PRIMARY KEY,
        "path" varchar(512) UNIQUE NOT NULL,
	"mirrors" smallint[]
);

-- run the following (shell) command:
-- createlang plpgsql <dbname>

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
$$ LANGUAGE plpgsql;


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
$$ LANGUAGE plpgsql;


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
$$ LANGUAGE plpgsql;

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
$$ LANGUAGE plpgsql;


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
$$ LANGUAGE plpgsql;



CREATE OR REPLACE FUNCTION mirr_get_name(integer) RETURNS text AS '
    SELECT identifier FROM server WHERE id=$1
' LANGUAGE SQL;


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
$$ LANGUAGE plpgsql;


-- now, fill the new database by scanning
-- restart Apache with the mod_mirrorbrain 2.7 module
-- verify that everything works

drop function from_unixtime(integer);
drop table file_server cascade;
drop table file;
