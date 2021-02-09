WITH_VIDEO_DECODING=1

OBJECTS=timg.o terminal-canvas.o image-display.o renderer.o

MAGICK_CXXFLAGS=$(shell GraphicsMagick++-config --cppflags)
CXXFLAGS=$(MAGICK_CXXFLAGS) -Wall -Wextra -W -Wno-unused-parameter -O3 -fPIC -std=c++14
LDFLAGS+=$(shell GraphicsMagick++-config --ldflags --libs)

ifneq ($(WITH_VIDEO_DECODING), 0)
  AV_CXXFLAGS=$(shell pkg-config --cflags  libavcodec libavformat libswscale libavutil)
  CXXFLAGS+=$(AV_CXXFLAGS) -DWITH_TIMG_VIDEO
  LDFLAGS+=$(shell pkg-config --cflags --libs  libavcodec libavformat libswscale libavutil)
  OBJECTS+=video-display.o
endif

PREFIX?=/usr/local

timg : $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

timg.o : timg-version.h

timg-version.h: .FORCE
	@(echo "#define TIMG_VERSION \"$(shell git describe --tags --match='v*' 2>/dev/null) $(shell git log -n1 --date=short --format='%cd' 2>/dev/null || echo -n '0.9.9+')\"" > $@-new; \
	cmp -s $@ $@-new || cp $@-new $@; \
	rm $@-new)

install: timg ../man/timg.1
	install -d $(PREFIX)/bin $(PREFIX)/share/man/man1
	install -s timg $(PREFIX)/bin/timg
	install ../man/timg.1 $(PREFIX)/share/man/man1/timg.1

uninstall:
	rm -f $(PREFIX)/bin/timg $(PREFIX)/share/man/man1/timg.1

clean:
	rm -f timg $(OBJECTS) timg-version.h

.FORCE:
