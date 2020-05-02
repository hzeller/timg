MAGICK_CXXFLAGS=$(shell GraphicsMagick++-config --cppflags)
MAGICK_LDFLAGS=$(shell GraphicsMagick++-config --ldflags --libs)
CXXFLAGS=$(MAGICK_CXXFLAGS) -Wall -Wextra -W -Wno-unused-parameter -O3 -fPIC -std=c++11
PREFIX?=/usr/local
OBJECTS=timg.o terminal-canvas.o image-display.o

timg : $(OBJECTS)
	$(CXX) -o $@ $^ $(MAGICK_LDFLAGS)

install: timg
	install $^ $(PREFIX)/bin

uninstall:
	rm -f $(PREFIX)/bin/timg

clean:
	rm -f timg $(OBJECTS)
