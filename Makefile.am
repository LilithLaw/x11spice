SUBDIRS = src
if DUMMY
    SUBDIRS += spice-video-dummy
endif

rpm: dist
	rpmbuild -ba --define "_sourcedir $$PWD" src/data/x11spice.spec
