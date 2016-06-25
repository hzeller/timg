CXXFLAGS=-Wall -O3 -fPIC
MAGICK_CXXFLAGS=`GraphicsMagick++-config --cppflags --cxxflags`
MAGICK_LDFLAGS=`GraphicsMagick++-config --ldflags --libs`
PREFIX?=/usr/local

timg : timg.o terminal-canvas.o
	$(CXX) -o $@ $^ $(MAGICK_LDFLAGS)

timg.o: timg.cc
	$(CXX) $(MAGICK_CXXFLAGS) $(CXXFLAGS) -c -o $@ $^

install: timg
	install $^ $(PREFIX)/bin

clean:
	rm -f timg terminal-canvas.o timg.o
