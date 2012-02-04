include ../../Makefile.include
include ../depends.mk

SOURCE=../../../../
# configuration settings
export SDKSTAGE

export PYTHON_VERSION=2.7
export PYTHON_LDFLAGS=-L$(SDKSTAGE)/usr/lib -lpython$(PYTHON_VERSION) -lpthread -ldl -lutil -lm
export PYTHON_CPPFLAGS=-I$(SDKSTAGE)/usr/include/python$(PYTHON_VERSION)
export PYTHON_SITE_PKG=$(SDKSTAGE)/usr/lib/python$(PYTHON_VERSION)/site-packages
export PYTHON_NOVERSIONCHECK=no-check
export USE_TEXTUREPACKER_NATIVE_ROOT=$(BUILDROOT)/output/host

export PATH:=$(SYSROOT)/usr/bin:$(PATH)
CONFIGURE=./configure --prefix=$(PREFIX) --host=$(HOST) \
  --enable-neon --enable-external-python --enable-gles --disable-sdl --disable-x11 --disable-xrandr \
  --disable-optical-drive --disable-dvdcss --enable-external-ffmpeg --enable-debug \
  --with-platform=amlogic-m1

all: configure

clean:
distclean:
	cd $(SOURCE);

configure:
	cd $(SOURCE); ./bootstrap
	cd $(SOURCE); $(CONFIGURE)
	#cd $(SOURCE); make -j $(JOBS)
	#cd $(SOURCE); make install

