
test_mb:
	 ( cd mb && PYTHONPATH=. python -m unittest discover -p '*tests.py' tests -v )

test_docker:
	bash t/test_docker.sh
