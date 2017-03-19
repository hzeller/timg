MAGICK_CXXFLAGS=$(shell GraphicsMagick++-config --cppflags)
MAGICK_LDFLAGS=$(shell GraphicsMagick++-config --ldflags --libs)
CXXFLAGS=$(MAGICK_CXXFLAGS) -Wall -O3 -fPIC
PREFIX?=/usr/local

timg : timg.o terminal-canvas.o
	$(CXX) -o $@ $^ $(MAGICK_LDFLAGS)

install: timg
	install $^ $(PREFIX)/bin

clean:
	rm -f timg timg.o terminal-canvas.o
