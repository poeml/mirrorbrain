-- jw, Fri Jan 19 20:22:42 CET 2007
-- manually added indices
--
CREATE INDEX file_path_idx ON file (path);
CREATE INDEX file_server_fileid_idx ON file_server (fileid);
CREATE INDEX file_server_serverid_idx ON file_server (serverid);
CREATE INDEX file_server_fileid_serverid_idx ON file_server (fileid,serverid);

-- for use_md5:
ALTER TABLE file_server ADD COLUMN path_md5 BINARY(22);
CREATE INDEX file_server_path_md5_idx ON file_server (path_md5);


