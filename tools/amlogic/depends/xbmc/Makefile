include ../../Makefile.include
include ../depends.mk

SOURCE=../../../../
# configuration settings
export SDKSTAGE
export ALSA_CFLAGS=
export PKG_CONFIG_PATH=$(PREFIX)/bin/pkg-config
CONFIGURE=./configure --prefix=$(PREFIX) --host=$(HOST) \
  --enable-external-python --enable-gles --disable-xrandr \
  --disable-optical-drive --disable-dvdcss

all: configure

clean:
distclean:
	cd $(SOURCE);

configure:
	cd $(SOURCE); ./bootstrap
	cd $(SOURCE); $(CONFIGURE)
	#cd $(SOURCE); make -j $(JOBS)
	#cd $(SOURCE); make install

