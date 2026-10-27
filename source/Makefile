# Builds the Pico Cheat Parasite: picoLoader7.bin and picoLoader9_$(PICO_PLATFORM).bin.
#
# The blobs are built first, since the loaders embed them: the in-game cheat menu and its resident part go into
# picoLoader7.bin, the installer goes into picoLoader9.bin.

.PHONY: all libraries blobs loader7 loader9 clean

PICO_PLATFORM ?= DSPICO

all: loader7 loader9

libraries:
	$(MAKE) -C libs/libtwl

blobs:
	$(MAKE) -f Makefile.blob NAME=cheatMenu SOURCEDIR=menu
	$(MAKE) -f Makefile.blob NAME=resident SOURCEDIR=resident
	$(MAKE) -f Makefile.installer

loader7: libraries blobs
	$(MAKE) -f Makefile.arm7

loader9: libraries blobs
	$(MAKE) -f Makefile.arm9 PLATFORM=$(PICO_PLATFORM)

clean:
	$(MAKE) -C libs/libtwl clean
	$(MAKE) -f Makefile.arm7 clean
	$(MAKE) -f Makefile.arm9 clean
	$(MAKE) -f Makefile.blob NAME=cheatMenu SOURCEDIR=menu clean
	$(MAKE) -f Makefile.blob NAME=resident SOURCEDIR=resident clean
	$(MAKE) -f Makefile.installer clean
	rm -rf build arm7/data arm9/data
