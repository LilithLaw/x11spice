SUBDIRS = src
if DUMMY
    SUBDIRS += spice-video-dummy
endif

rpm: dist
	rpmbuild -ta $(DIST_ARCHIVES)
