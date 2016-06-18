CXXFLAGS=-Wall -O3
MAGICK_CXXFLAGS=`GraphicsMagick++-config --cppflags --cxxflags`
MAGICK_LDFLAGS=`GraphicsMagick++-config --ldflags --libs`
PREFIX?=/usr/local

timg : timg.cc terminal-canvas.o
	$(CXX) $(CXXFLAGS) $(MAGICK_CXXFLAGS) -o $@ $^ $(MAGICK_LDFLAGS)

install: timg
	install $^ $(PREFIX)/bin

clean:
	rm -f timg terminal-canvas.o
