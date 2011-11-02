#
# amlogic test project.
#  Usage:
#    ./aml_decoder --input media/SpeedRacer.mov
#

include ../../Makefile.include
include ../depends.mk

BIN = aml_decoder

LIBS+= -lz -lc -lbz2 -pthread -lamcodec -lavformat -lavcodec -lavutil
CPPFLAGS+= -g -O0 -Wall -D__STDC_CONSTANT_MACROS
CPPFLAGS+= -mcpu=cortex-a9 -mfpu=neon

SRCFILES = aml_decoder.cpp \
           file_reader_util.cpp \
           bitstream_converter.cpp \
           ffmpeg_file_protocol.cpp

OBJFILES = ${SRCFILES:.cpp=.o}

all:: $(BIN)

$(BIN): $(OBJFILES)
	$(CXX) $(LDFLAGS) -o $(BIN) $(OBJFILES) $(LIBS)

clean::
	rm -f $(BIN) $(OBJFILES)

distclean:: clean



