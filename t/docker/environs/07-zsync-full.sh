#!../lib/test-in-container-environs.sh
set -ex

./environ.sh pg9-system2
./environ.sh ap9-system2
./environ.sh ap8-system2
./environ.sh ap7-system2
./environ.sh mb9 $(pwd)/mirrorbrain

pg9*/start.sh

mb9*/configure_db.sh pg9
mb9*/configure_apache.sh ap9

size=${BIG_FILE_SIZE:-100M}
file=.product/mb/.data/file$size

mkdir -p .product/mb/.data
[ -f $file ] || ! which fallocate || fallocate -l $size $file
[ -f $file ] || dd if=/dev/zero of=./$file bs=4K iflag=fullblock,count_bytes count=$size

sed -i '/dbname = mirrorbrain/a zsync_hashes = 1' mb9*/mirrorbrain.conf

ap9=$(ls -d ap9*)

# populate test data
for x in ap7 ap8 ap9; do
    xx=$(ls -d $x*/)
    mkdir -p $xx/dt/downloads/
    # add the file only to one mirror
    [ $x == ap7 ] || ln $file $xx/dt/downloads/
done

mb9*/mb.sh makehashes -v $PWD/ap9-system2/dt/

for x in ap7 ap8; do
    $x*/start.sh
    $x*/status.sh
    mb9*/mb.sh new $x --http http://"$($x-system2/print_address.sh)" --region NA --country us
    mb9*/mb.sh scan --enable $x
done

ap9*/start.sh
checksum=$(ap9*/curl.sh downloads/file$size.zsync | tail -n +4 | md5sum -)
# compare to checksum, which should be correct one for the file
[ $size != 100M ] || test "$checksum" == "d0e0f1362080c568137254d190ba2421  -"

tail ap9*/dt/error_log | grep 'Found 1 mirror'
