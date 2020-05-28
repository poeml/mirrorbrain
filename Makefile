
test_mb:
	 ( cd mb && PYTHONPATH=. python3 -m unittest discover -p '*tests.py' tests -v )

test_docker:
	bash t/test_docker.sh

test_environs:
	bash t/test_environs.sh

build_install:
	meson setup build
	meson configure --prefix=/usr --datadir=/usr/share -Dmemcached=false -Dinstall-icons=false build
	ninja -v -C build
	rm -rf build
	meson setup build
	meson configure --prefix=/usr --datadir=/usr/share -Dmemcached=true -Dinstall-icons=false build
	ninja -v -C build
	DESTDIR=$(DESTDIR) ninja -v -C build install
