WITH_VIDEO_DECODING=1

OBJECTS=timg.o terminal-canvas.o image-display.o

MAGICK_CXXFLAGS=$(shell GraphicsMagick++-config --cppflags)
MAGICK_LDFLAGS=$(shell GraphicsMagick++-config --ldflags --libs)
CXXFLAGS=$(MAGICK_CXXFLAGS) -Wall -Wextra -W -Wno-unused-parameter -O3 -fPIC -std=c++11

ifneq ($(WITH_VIDEO_DECODING), 0)
  AV_CXXFLAGS=$(shell pkg-config --cflags  libavcodec libavformat libswscale libavutil)
  AV_LDFLAGS=$(shell pkg-config --cflags --libs  libavcodec libavformat libswscale libavutil)
  CXXFLAGS+=$(AV_CXXFLAGS) -Wno-deprecated-declarations -DWITH_TIMG_VIDEO
  LDFLAGS+=$(AV_LDFLAGS)
  OBJECTS+=video-display.o
endif

PREFIX?=/usr/local

timg : $(OBJECTS)
	$(CXX) -o $@ $^ $(MAGICK_LDFLAGS) $(AV_LDFLAGS)

install: timg
	install $^ $(PREFIX)/bin

uninstall:
	rm -f $(PREFIX)/bin/timg

clean:
	rm -f timg $(OBJECTS)
