#!../lib/test-in-container-environs.sh
set -ex

./environ.sh mb9 $(pwd)/mirrorbrain
./environ.sh pg9-system2

pg9*/start.sh

mb9*/configure_db.sh pg9

size=${BIG_FILE_SIZE:-100M}
file=.product/mb/.data/file$size

mkdir -p .product/mb/.data
[ -f $file ] || ! which fallocate || fallocate -l $size $file
[ -f $file ] || dd if=/dev/zero of=./$file bs=4K iflag=fullblock,count_bytes count=$size

mkdir -p mb9/downloads

ln $file mb9/downloads/

sed -i '/dbname = mirrorbrain/a zsync_hashes = 1' mb9*/mirrorbrain.conf

mb9*/mb.sh makehashes $PWD/mb9/downloads
# test with checksum of zsums receved for 100M empty file with 2.19.3
test $file != .product/mb/.data/file100M || test 336c175872f11712eaa1f7a97d8942ab == $(pg9*/sql.sh -t -c "select md5(zsums) from files" mirrorbrain)
pg9*/sql.sh -c "select path, sha1, length(sha1pieces) as sha1pieceslen, zblocksize, zhashlens, length(zsums) as zsumslen from files" mirrorbrain

mb9*/mb.sh makehashes -v --zsync-mask '.*' $PWD/mb9/downloads
pg9*/sql.sh -c "select path, sha1, length(sha1pieces) as sha1pieceslen, zblocksize, zhashlens, length(zsums) as zsumslen from files" mirrorbrain
