include ../Makefile.include
include ../android-sdk.mk

SOURCE=../xbmc
# configuration settings
export CXXFLAGS+=-isystem$(SDKSTAGE)/include/linux_user
CONFIGURE=./configure --prefix=$(PREFIX) --host=$(HOST) \
  --enable-gles --disable-xrandr \
  --disable-optical-drive --disable-dvdcss --disable-samba

all: configure

configure:
	cd $(SOURCE); ./bootstrap
	cd $(SOURCE); $(CONFIGURE)
	cd $(SOURCE); make -j $(JOBS)
	cd $(SOURCE); make install
clean:
	cd $(SOURCE); make clean

distclean:
	cd $(SOURCE); make clean
