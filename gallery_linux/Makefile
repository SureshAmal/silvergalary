CC ?= gcc
CXX ?= g++
CFLAGS ?= -O3 -Wall -Wno-unused-function -Wno-unused-result
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wno-unused-function -Wno-unused-result
# Vendored stb/NanoSVG releases trigger a handful of GCC diagnostics inside
# their implementation. Keep these suppressions local to thirdparty.o so our
# own code continues to build with the full warning set.
VENDOR_WARNINGS = -Wno-sign-compare -Wno-unused-but-set-variable \
	-Wno-maybe-uninitialized -Wno-stringop-overflow
INCLUDES = -I../include -I../include/easyexif -I../include/GLFW -I../viewer_linux
LIBS = ../lib/sqlite3.o ../lib/libglfw3.a -lGL -lm -lpthread -ldl

# ---------------------------------------------------------------------------
# Optional accelerated / extra-format codecs (auto-detected, never required)
#   libjpeg-turbo : DCT-scaled JPEG thumbnail decoding
#   libwebp       : WebP decoding with native downscaling
#   libavif       : AVIF decoding
# SVG always works through the vendored nanosvg rasterizer.
# ---------------------------------------------------------------------------
PKG_CONFIG ?= pkg-config

ifeq ($(shell $(PKG_CONFIG) --exists libturbojpeg && echo 1),1)
CXXFLAGS += -DHAVE_TURBOJPEG
INCLUDES += $(shell $(PKG_CONFIG) --cflags libturbojpeg)
LIBS += $(shell $(PKG_CONFIG) --libs libturbojpeg)
endif

ifeq ($(shell $(PKG_CONFIG) --exists libwebp && echo 1),1)
CXXFLAGS += -DHAVE_WEBP
INCLUDES += $(shell $(PKG_CONFIG) --cflags libwebp)
LIBS += $(shell $(PKG_CONFIG) --libs libwebp)
else
# Fedora installs the versioned runtime separately from libwebp-devel. The
# repository already vendors the public headers, so use the runtime soname when
# it is available instead of advertising WebP and then handing it to stb_image
# (which cannot decode WebP).
WEBP_RUNTIME := $(shell $(CXX) -print-file-name=libwebp.so.7)
ifneq ($(WEBP_RUNTIME),libwebp.so.7)
CXXFLAGS += -DHAVE_WEBP
INCLUDES += -I../include/webp
LIBS += -l:libwebp.so.7
endif
endif

ifeq ($(shell $(PKG_CONFIG) --exists libavif && echo 1),1)
CXXFLAGS += -DHAVE_AVIF
INCLUDES += $(shell $(PKG_CONFIG) --cflags libavif)
LIBS += $(shell $(PKG_CONFIG) --libs libavif)
endif

# HEIC/HEIF, JPEG XL, TIFF and camera RAW. All optional: without the library the
# format is simply never offered, rather than listed and then failing to open.
ifeq ($(shell $(PKG_CONFIG) --exists libheif && echo 1),1)
CXXFLAGS += -DHAVE_HEIF
INCLUDES += $(shell $(PKG_CONFIG) --cflags libheif)
LIBS += $(shell $(PKG_CONFIG) --libs libheif)
endif

ifeq ($(shell $(PKG_CONFIG) --exists libjxl && echo 1),1)
CXXFLAGS += -DHAVE_JXL
INCLUDES += $(shell $(PKG_CONFIG) --cflags libjxl)
LIBS += $(shell $(PKG_CONFIG) --libs libjxl)
endif

ifeq ($(shell $(PKG_CONFIG) --exists libtiff-4 && echo 1),1)
CXXFLAGS += -DHAVE_TIFF
INCLUDES += $(shell $(PKG_CONFIG) --cflags libtiff-4)
LIBS += $(shell $(PKG_CONFIG) --libs libtiff-4)
endif

ifeq ($(shell $(PKG_CONFIG) --exists libraw && echo 1),1)
CXXFLAGS += -DHAVE_RAW
INCLUDES += $(shell $(PKG_CONFIG) --cflags libraw)
LIBS += $(shell $(PKG_CONFIG) --libs libraw)
endif

# Colour management. Without it an image carrying a Display P3 or Adobe RGB
# profile is drawn as though its numbers were sRGB, which is a wrong picture
# rather than a missing feature.
ifeq ($(shell $(PKG_CONFIG) --exists lcms2 && echo 1),1)
CXXFLAGS += -DHAVE_LCMS
INCLUDES += $(shell $(PKG_CONFIG) --cflags lcms2)
LIBS += $(shell $(PKG_CONFIG) --libs lcms2)
endif

# ---------------------------------------------------------------------------
# Text stack: FreeType (rasterization) + HarfBuzz (shaping) + fontconfig
# (font discovery). Optional - without them FontRenderer keeps the
# stb_truetype ASCII path.
#
# NOTE: these cflags are placed BEFORE -I../include on the compile line,
# because the repo carries a stale vendored include/ft2build.h that would
# otherwise shadow the system FreeType headers and mismatch the linked lib.
# ---------------------------------------------------------------------------
PKG_CFLAGS =

ifeq ($(shell $(PKG_CONFIG) --exists harfbuzz freetype2 && echo 1),1)
CXXFLAGS += -DHAVE_HARFBUZZ
PKG_CFLAGS += $(shell $(PKG_CONFIG) --cflags harfbuzz freetype2)
LIBS += $(shell $(PKG_CONFIG) --libs harfbuzz freetype2)
ifeq ($(shell $(PKG_CONFIG) --exists fontconfig && echo 1),1)
CXXFLAGS += -DHAVE_FONTCONFIG
PKG_CFLAGS += $(shell $(PKG_CONFIG) --cflags fontconfig)
LIBS += $(shell $(PKG_CONFIG) --libs fontconfig)
endif
endif

SRCS = main.cpp
# Vendored code, built once and shared by both binaries.
VENDOR = ../lib/thirdparty.o ../lib/exif.o
GENERATED = ../include/silver_config_default.h
HEADERS = $(wildcard *.h) $(wildcard ../viewer_linux/*.h) $(wildcard ../include/*.h)
TARGET = ../bin/silver_gallery

all: $(TARGET)

# The built-in defaults are generated from the real config file, so the two can
# never drift. Previously silver_config.h carried a hand-maintained second copy;
# a duplicate key in it silently shadowed a setting, and every new option had to
# be added in two places.
../include/silver_config_default.h: ../config/silver.defaults.json
	@printf '#pragma once\n\n// GENERATED FILE - do not edit.\n// Built from config/silver.json by the Makefile. Edit that file instead.\n\nstatic const char* kSilverDefaultConfig = R"JSON(\n' > $@
	@cat $< >> $@
	@printf ')JSON";\n' >> $@
	@echo "Generated $@"


# The vendored single-header libraries, built once. They are ~24k lines that
# never change; recompiling them with every edit was most of the build time.
../lib/exif.o: ../include/easyexif/exif.cpp ../include/easyexif/exif.h
	@mkdir -p ../lib
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

../lib/thirdparty.o: ../src/silver_thirdparty.cpp ../include/stb_image.h ../include/stb_truetype.h ../include/stb_image_resize.h ../include/stb_image_write.h ../include/stb_rect_pack.h ../include/nanosvg.h ../include/nanosvgrast.h
	@mkdir -p ../lib
	$(CXX) $(CXXFLAGS) $(VENDOR_WARNINGS) $(PKG_CFLAGS) $(INCLUDES) -c $< -o $@

../lib/sqlite3.o: ../include/sqlite3.c
	@mkdir -p ../lib
	$(CC) $(CFLAGS) -I../include -c $< -o $@

$(TARGET): $(GENERATED) $(SRCS) $(HEADERS) $(VENDOR) ../lib/sqlite3.o ../lib/libglfw3.a
	@mkdir -p ../bin
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) $(INCLUDES) $(SRCS) $(VENDOR) $(LIBS) -o $(TARGET)
	@ln -sf silver_gallery ../bin/silver_gallery_linux
	@echo "Gallery build successful: $(TARGET)"

clean:
	rm -f $(GENERATED) $(VENDOR) $(TARGET) ../bin/silver_gallery_linux

run: all
	$(TARGET)

.PHONY: all clean run
