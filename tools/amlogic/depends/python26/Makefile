include ../../Makefile.include
include ../depends.mk

# lib name, version
LIBNAME=Python
VERSION=2.6.5
SOURCE=$(LIBNAME)-$(VERSION)
ARCHIVE=$(SOURCE).tar.bz2

# configuration settings
DISABLE_MODS="_ctypes readline _curses _curses_panel _tkinter nis gdbm bsddb _codecs_kr _codecs_jp _codecs_cn _codecs_tw _codecs_hk"

CONFIGURE_NATIVE= CFLAGS=" " CXXFLAGS=" " LDFLAGS=" " CPPFLAGS=" " \
  CPP="cpp" \
  CC="gcc" \
  CXX="g++" \
  LD="ld" \
  AR="ar" \
  RANLIB="ranlib" \
  RLINK_PATH=" " \
  EXTRA_CFLAGS=" " \
  ./configure

CONFIGURE=EXTRA_CFLAGS="$(CFLAGS)" ./configure --prefix=$(PREFIX) --host=$(HOST) \
  --enable-unicode=ucs4 --enable-shared

LIBDYLIB=$(SOURCE)/libpython2.6.so

CLEAN_FILES=$(ARCHIVE) $(SOURCE)

all: $(LIBDYLIB) .installed

$(TARBALLS_LOCATION)/$(ARCHIVE):
	$(RETRIEVE_TOOL) $(RETRIEVE_TOOL_FLAGS) $(BASE_URL)/$(ARCHIVE)

$(LIBDYLIB): $(TARBALLS_LOCATION)/$(ARCHIVE)
	rm -rf $(SOURCE)
	$(ARCHIVE_TOOL) $(ARCHIVE_TOOL_FLAGS) $(TARBALLS_LOCATION)/$(ARCHIVE)
	echo $(SOURCE) > .gitignore
	cd $(SOURCE); patch -p0 < ../Python-2.6.5-disable-modules.patch
	cd $(SOURCE); patch -p1 < ../Python-2.6-ctypes.patch
	cd $(SOURCE); $(CONFIGURE_NATIVE)
	cd $(SOURCE); make python Parser/pgen
	cd $(SOURCE); mv python hostpython
	cd $(SOURCE); mv Parser/pgen Parser/hostpgen
	cd $(SOURCE); make distclean
	cd $(SOURCE); patch -p1 < ../Python-2.6.5-xcompile.patch
	cd $(SOURCE); $(CONFIGURE)
	cd $(SOURCE); make -j $(JOBS) HOSTPYTHON=./hostpython HOSTPGEN=./Parser/hostpgen CROSS_COMPILE_TARGET=yes PYTHON_DISABLE_MODULES=$(DISABLE_MODS)

.installed:
	cd $(SOURCE); make install HOSTPYTHON=./hostpython HOSTPGEN=./Parser/hostpgen CROSS_COMPILE_TARGET=yes
	find $(PREFIX)/lib/python2.6 -type f -name *.pyc -exec rm -f {} \;
	find $(PREFIX)/lib/python2.6 -type f -name *.pyo -exec rm -f {} \;
	touch $@

clean:
	rm -rf $(SOURCE) .installed

distclean::
	rm -rf $(SOURCE) .installed
