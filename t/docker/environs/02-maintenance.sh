#!../lib/test-in-container-environs.sh
set -ex

[ -d mirrorbrain ]

./environ.sh pg9-system2
./environ.sh ap9-system2
./environ.sh ap8-system2
./environ.sh ap7-system2
./environ.sh mb9 $(pwd)/mirrorbrain

pg9*/start.sh

mb9*/configure_db.sh pg9
mb9*/configure_apache.sh ap9

ap9=$(ls -d ap9*)

# populate test data
for x in ap7 ap8 ap9; do
    xx=$(ls -d $x*/)
    mkdir -p $xx/dt/downloads/{folder1,folder2,folder3}
    echo $xx/dt/downloads/{folder1,folder2,folder3}/{file1,file2}.dat | xargs -n 1 touch
done

mkdir -p ap9-system2/hashes

mb9*/mb.sh makehashes $PWD/ap9-system2/dt/

ap9*/start.sh
ap9*/curl.sh downloads/ | grep folder1

for x in ap7 ap8; do
    $x*/start.sh
    $x*/status.sh
    mb9*/mb.sh new $x --http http://"$($x-system2/print_address.sh)" --region NA --country us
    mb9*/mb.sh scan --enable $x
    $x-system2/curl.sh | grep downloads
done

ap9*/curl.sh /downloads/folder1/file1.dat

tail ap9*/dt/error_log | grep 'Chose server '

test 3 == $(mb9/mb.sh file ls *file1.dat | grep ap7 | wc -l)
test 3 == $(mb9/mb.sh dirs ap8 | grep downloads | wc -l)

for x in ap7 ap8 ap9; do
    xx=$(ls -d $x*/)
    rm -r $xx/dt/downloads/folder2
    [ $x == ap9 ] || mb9*/mb.sh scan $x
done

test 2 == $(mb9/mb.sh file ls *file1.dat | grep ap7 | wc -l)
test 2 == $(mb9/mb.sh dirs ap8 | grep downloads | wc -l)

mv ap9*/dt/*log ap9*/
mb9*/mb.sh makehashes $PWD/ap9-system2/dt/ 

mb9*/mb.sh db vacuum -n | grep 'Stale files' | grep '\b2\b'
mb9*/mb.sh db vacuum | grep 'Done.'
mb9/mb.sh db vacuum -n | grep 'Stale files' | grep '\b0\b'

rm ap7-system2/dt/downloads/folder3/file1.dat
mb9/mb.sh scan ap7
test 1 == $(mb9/mb.sh file ls *file1.dat | grep ap7 | wc -l)
test ap7 == $(mb9/mb.sh dirs --missing downloads/folder3/file1.dat)
test ap8 == $(mb9/mb.sh dirs -d downloads/folder3/file1.dat)
test 2 == $(mb9/mb.sh dirs -d downloads/folder1/file1.dat | wc -l)
