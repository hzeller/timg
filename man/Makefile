
timg.1.gz:

# Using https://github.com/rtomayko/ronn to generate a manpage from
# a markdown.
# A pre-generated gzipped manpage will be checked in to minimize dependencies
# for building.
timg.1: timg.1.md
	ronn < $^ > $@

%.gz : %
	gzip < $^ > $@
