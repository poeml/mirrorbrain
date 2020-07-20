INSERT INTO files (
    id,
    path,
    mtime,
    size,
    md5,
    sha1,
    sha256,
    sha1piecesize,
    sha1pieces,
    btih,
    pgp,
    zblocksize,
    zhashlens,
    zsums
  )
  OVERRIDING SYSTEM VALUE
  SELECT
    filearr.id,
    filearr.path,
    to_timestamp(hash.mtime) as mtime,
    hash.size::bigint,
    hash.md5::bytea,
    hash.sha1::bytea,
    hash.sha256::bytea,
    hash.sha1piecesize::integer,
    hash.sha1pieces::bytea,
    hash.btih::bytea,
    hash.pgp::text,
    hash.zblocksize::integer,
    hash.zhashlens::character varying(8),
    hash.zsums::bytea
  FROM filearr
    LEFT JOIN hash ON hash.file_id = filearr.id
  ORDER BY filearr.id;

-- TODO: documentation that this needs the intarray extension
INSERT INTO server_files (
    file_id,
    server_id
  )
  SELECT
    id AS file_id,
    unnest(mirrors & (select array_agg(id) from server)) AS server_id
  FROM filearr
  ORDER BY id;
