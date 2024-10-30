CUSTOM_CFLAGS=-Wall -Wno-deprecated-declarations -Wno-format-security -Wno-unused-result -Werror $(X11SPICE_ONLY_CFLAGS)
SUBDIRS = src
if DUMMY
    SUBDIRS += spice-video-dummy
endif

rpm: dist
	rpmbuild -ta $(DIST_ARCHIVES)
