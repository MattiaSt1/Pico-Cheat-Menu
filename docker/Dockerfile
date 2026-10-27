# Build environment of Pico Cheat Menu: the BlocksDS toolchain. Build.bat builds the image and runs make in it.
FROM skylyrac/blocksds:slim-v1.20.0
WORKDIR /work/source
CMD ["make", "-j4", "PICO_PLATFORM=DSPICO"]
