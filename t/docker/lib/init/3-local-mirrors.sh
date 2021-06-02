thisdir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

. $thisdir/install-from-source.sh

mirrors='mirrorA mirrorB mirrorC mirrorD mirrorE mirrorF'

for site in $mirrors; do
    setup_vhost $site $((port++))
done

systemctl start apache2

port=80
curl -s 127.0.0.1:80 | grep downloads

for site in $mirrors; do
    : $((port++))
    mb new $site --http http://127.0.0.1:$port --rsync rsync://127.0.0.1/$site/downloads --region NA --country us
    mb scan --enable $site
    curl -s 127.0.0.1:$port | grep downloads
done
