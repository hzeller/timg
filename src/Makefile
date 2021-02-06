WITH_VIDEO_DECODING=1

OBJECTS=timg.o terminal-canvas.o image-display.o

MAGICK_CXXFLAGS=$(shell GraphicsMagick++-config --cppflags)
CXXFLAGS=$(MAGICK_CXXFLAGS) -Wall -Wextra -W -Wno-unused-parameter -O3 -fPIC -std=c++11
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
	@((echo "#ifndef HAVE_CMAKE_GENERATED_VERSION_H" ; \
	  echo "#define TIMG_VERSION \"$(shell git describe --tags --match='v*' 2>/dev/null) $(shell git log -n1 --date=short --format='%cd' 2>/dev/null || echo -n '0.9.9+')\""; \
	  echo "#else"; \
	  echo "#include <timg-version-generated.h>"; \
	  echo "#endif" ) > $@-new; \
	cmp -s $@ $@-new || cp $@-new $@; \
	rm $@-new)

install: timg ../man/timg.1.gz
	install -d $(PREFIX)/bin $(PREFIX)/share/man/man1
	install -s timg $(PREFIX)/bin/timg
	install ../man/timg.1.gz $(PREFIX)/share/man/man1/timg.1.gz

uninstall:
	rm -f $(PREFIX)/bin/timg $(PREFIX)/share/man/man1/timg.1.gz

clean:
	rm -f timg $(OBJECTS) timg-version.h

.FORCE:
