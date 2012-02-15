include ../Makefile.include
include ../android-sdk.mk

SOURCE=../../../../

# configuration settings
export CXXFLAGS+=-isystem$(SDKSTAGE)/include/linux_user
CONFIGURE=./configure --prefix=$(PREFIX) --host=$(HOST) \
  --enable-neon --enable-external-python --enable-gles --enable-external-ffmpeg --enable-debug \
  --disable-sdl --disable-x11 --disable-xrandr --disable-samba --disable-dvdcss \
  --with-platform=amlogic-m1

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
