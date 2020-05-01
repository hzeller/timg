MAGICK_CXXFLAGS=$(shell GraphicsMagick++-config --cppflags)
MAGICK_LDFLAGS=$(shell GraphicsMagick++-config --ldflags --libs)
CXXFLAGS=$(MAGICK_CXXFLAGS) -Wall -Wextra -W -Wno-unused-parameter -O3 -fPIC
PREFIX?=/usr/local

timg : timg.o terminal-canvas.o
	$(CXX) -o $@ $^ $(MAGICK_LDFLAGS)

install: timg
	install $^ $(PREFIX)/bin

uninstall:
	rm -f $(PREFIX)/bin/timg

clean:
	rm -f timg timg.o terminal-canvas.o
