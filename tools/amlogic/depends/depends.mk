export HOST=arm-none-linux-gnueabi
export BUILD=i686-linux
export PREFIX=$(XBMCPREFIX)
export SYSROOT=$(BUILDROOT)/output/host/usr/arm-unknown-linux-gnueabi/sysroot
export CFLAGS=-isystem$(PREFIX)/include
export CXXFLAGS=$(CFLAGS)
export CPPFLAGS=$(CFLAGS)
export LDFLAGS=-L$(XBMCPREFIX)/lib
export LD=$(TOOLCHAIN)/bin/$(HOST)-ld --sysroot=$(SYSROOT)
export CC=$(TOOLCHAIN)/bin/$(HOST)-gcc --sysroot=$(SYSROOT)
export CXX=$(TOOLCHAIN)/bin/$(HOST)-g++ --sysroot=$(SYSROOT)
export OBJDUMP=$(TOOLCHAIN)/bin/$(HOST)-objdump
export RANLIB=$(TOOLCHAIN)/bin/$(HOST)-ranlib
export STRIP=$(TOOLCHAIN)/bin/$(HOST)-strip
export AR=$(TOOLCHAIN)/bin/$(HOST)-ar
export CXXCPP=$(CXX) -E
export PKG_CONFIG_PATH=$(PREFIX)/lib/pkgconfig
export PATH:=$(PREFIX)/bin:$(BUILDROOT)/output/host/usr/bin:$(PATH)
