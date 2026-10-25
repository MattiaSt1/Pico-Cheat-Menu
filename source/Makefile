.PHONY: all libraries blobs loader7 loader9 clean
PICO_PLATFORM ?= DSPICO
all: loader7 loader9
libraries:
	$(MAKE) -C libs/libtwl
blobs:
	$(MAKE) -f Makefile.cheatmenu
	$(MAKE) -f Makefile.resident
	$(MAKE) -f Makefile.installer
loader7: libraries blobs
	$(MAKE) -f Makefile.arm7
loader9: libraries blobs
	$(MAKE) -f Makefile.arm9 PLATFORM=$(PICO_PLATFORM)
clean:
	$(MAKE) -C libs/libtwl clean
	$(MAKE) -f Makefile.arm7 clean
	$(MAKE) -f Makefile.arm9 clean
	$(MAKE) -f Makefile.cheatmenu clean
	$(MAKE) -f Makefile.resident clean
	$(MAKE) -f Makefile.installer clean
	rm -rf build arm7/data arm9/data
