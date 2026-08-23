all: viewer gallery

viewer:
	@$(MAKE) -C viewer_linux

gallery:
	@$(MAKE) -C gallery_linux

clean:
	@$(MAKE) -C viewer_linux clean
	@$(MAKE) -C gallery_linux clean

run: viewer
	@$(MAKE) -C viewer_linux run

run_gallery: gallery
	@$(MAKE) -C gallery_linux run

.PHONY: all viewer gallery clean run run_gallery
