all: thirdparty viewer gallery

# Built once and shared by both binaries. Made an explicit target so a parallel
# `make -j` cannot have both sub-makes racing to produce the same object file.
thirdparty:
	@$(MAKE) -C gallery_linux ../lib/thirdparty.o
	@$(MAKE) -C gallery_linux ../lib/exif.o

viewer: thirdparty
	@$(MAKE) -C viewer_linux

gallery: thirdparty
	@$(MAKE) -C gallery_linux

clean:
	@rm -f lib/thirdparty.o lib/exif.o
	@$(MAKE) -C viewer_linux clean
	@$(MAKE) -C gallery_linux clean

run: viewer
	@$(MAKE) -C viewer_linux run

run_gallery: gallery
	@$(MAKE) -C gallery_linux run

.PHONY: all thirdparty viewer gallery clean run run_gallery
