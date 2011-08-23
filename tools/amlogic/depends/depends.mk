export HOST=arm-none-linux-gnueabi
export BUILD=i686-linux
export PREFIX=${XBMCPREFIX}
export TARGETFS
export RLINK_PATH=-Wl,-rpath-link,${TARGETFS}/lib -Wl,-rpath-link,${TARGETFS}/usr/lib

export CFLAGS=-isystem${XBMCPREFIX}/include -isystem${SDKSTAGE}/include -isystem${SDKSTAGE}/usr/include -isystem${SDKSTAGE}/usr/local/include -L${TARGETFS}/usr/lib -L${XBMCPREFIX}/lib ${RLINK_PATH}
export CXXFLAGS=${CFLAGS}
export CPPFLAGS=${CFLAGS}
export LDFLAGS=${RLINK_PATH} -L${TARGETFS}/usr/lib -L${XBMCPREFIX}/lib
export LD=${TOOLCHAIN}/bin/${HOST}-ld
export AR=${TOOLCHAIN}/bin/${HOST}-ar
export CC=${TOOLCHAIN}/bin/${HOST}-gcc
export CXX=${TOOLCHAIN}/bin/${HOST}-g++
export CXXCPP=${CXX} -E
export RANLIB=${TOOLCHAIN}/bin/${HOST}-ranlib
export STRIP=${TOOLCHAIN}/bin/${HOST}-strip
export OBJDUMP=${TOOLCHAIN}/bin/${HOST}-objdump 
export ACLOCAL=aclocal -I ${PREFIX}/share/aclocal -I ${PREFIX}/share/aclocal-1.10 -I ${TARGETFS}/usr/share/aclocal
export PKG_CONFIG_LIBDIR=${PREFIX}/lib/pkgconfig:${SDKSTAGE}/lib/pkgconfig:${SDKSTAGE}/usr/lib/pkgconfig
export PATH:=${PREFIX}/bin:$(PATH):${TOOLCHAIN}/bin
