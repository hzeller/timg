
timg.1:

# Using pandoc to generate a manpage from a markdown.
# A pre-generated manpage will be checked in to minimize dependencies
# for building.
timg.1: timg.1.md
	pandoc $^ -s -t man -o $@
