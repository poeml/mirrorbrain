ALTER TABLE filearr ADD COLUMN dirname  character varying(512);
ALTER TABLE filearr ADD COLUMN filename character varying(512);

CREATE INDEX filearr_dirname_btree ON filearr USING btree(dirname);

DROP FUNCTION IF EXISTS path_split_trigger() CASCADE;

CREATE FUNCTION path_split_trigger() RETURNS TRIGGER AS $$
BEGIN
    IF ( NEW.dirname is null or NEW.filename is null or (NEW.dirname || NEW.filename <> NEW.path )) THEN
        RAISE DEBUG 'need to create a new splitted path for %', NEW.path; 
        NEW.dirname := substring(NEW.path, '^.+/');
        NEW.filename := substring(NEW.path, '[^/]+$' );
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE 'plpgsql';

CREATE TRIGGER trigger_path_split before INSERT OR UPDATE ON filearr FOR EACH ROW EXECUTE PROCEDURE path_split_trigger();
-- update all existing records
UPDATE filearr SET id=id where dirname is null;
