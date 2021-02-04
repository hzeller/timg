WITH_VIDEO_DECODING=1

OBJECTS=timg.o terminal-canvas.o image-display.o

MAGICK_CXXFLAGS=$(shell GraphicsMagick++-config --cppflags)
MAGICK_LDFLAGS=$(shell GraphicsMagick++-config --ldflags --libs)
CXXFLAGS=$(MAGICK_CXXFLAGS) -Wall -Wextra -W -Wno-unused-parameter -O3 -fPIC -std=c++11

ifneq ($(WITH_VIDEO_DECODING), 0)
  AV_CXXFLAGS=$(shell pkg-config --cflags  libavcodec libavformat libswscale libavutil)
  AV_LDFLAGS=$(shell pkg-config --cflags --libs  libavcodec libavformat libswscale libavutil)
  CXXFLAGS+=$(AV_CXXFLAGS) -DWITH_TIMG_VIDEO
  LDFLAGS+=$(AV_LDFLAGS)
  OBJECTS+=video-display.o
endif

PREFIX?=/usr/local

timg : $(OBJECTS)
	$(CXX) -o $@ $^ $(MAGICK_LDFLAGS) $(AV_LDFLAGS)

timg.o : timg-version.h

timg-version.h: .FORCE
	@(echo "#define TIMG_VERSION \"$(shell git describe --tags --match='v*') $(shell git log -n1 --date=short --format='%cd' 2>/dev/null || echo -n '0.9.8+')\"" > $@-new; \
	cmp -s $@ $@-new || cp $@-new $@; \
	rm $@-new)

install: timg ../man/timg.1.gz
	install -D -s timg $(PREFIX)/bin/timg
	install -D ../man/timg.1.gz $(PREFIX)/share/man/man1/timg.1.gz

uninstall:
	rm -f $(PREFIX)/bin/timg

clean:
	rm -f timg $(OBJECTS) timg-version.h

.FORCE:
