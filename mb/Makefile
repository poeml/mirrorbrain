PY_FILES=mb.py mb/*.py
PYC_FILES=$(wildcard *.pyc mb/*.pyc mb/crawlers/__pycache__/ mb/__pycache__/ __pycache__/)

all: $(PY_FILES)
	python3 -m compileall $^

clean: $(PYCF_FILES)
ifdef PYC_FILES
	rm -rv $(PYC_FILES)
endif
