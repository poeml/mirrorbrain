cd "$(dirname "${BASH_SOURCE[0]}")"

(
dockerfail=0
cd docker
for t in *.sh; do
    [ -x "$t" ] || continue
    ./$t
    if [ $? -eq 0 ] ; then
        echo "P:$t"
    else
        : $((dockerfail++))
        echo "F-$t"
    fi
done
exit $dockerfail
)
dockerfail=$?
( exit $dockerfail )
