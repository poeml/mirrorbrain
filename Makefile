
test_mb:
	 ( cd mb && PYTHONPATH=. python3 -m unittest discover -p '*tests.py' tests -v )

test_docker:
	bash t/test_docker.sh

build_install:
	meson setup --wipe build
	meson --prefix=/usr --datadir=/usr/share -Dmemcached=true build
	ninja -v -C build
	DESTDIR=$(DESTDIR) ninja -v -C build install
