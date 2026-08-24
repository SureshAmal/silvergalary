CC ?= gcc
CXX ?= g++
CFLAGS ?= -O3 -Wall -Wno-unused-function -Wno-unused-result
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wno-unused-function -Wno-unused-result
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
endif

ifeq ($(shell $(PKG_CONFIG) --exists libavif && echo 1),1)
CXXFLAGS += -DHAVE_AVIF
INCLUDES += $(shell $(PKG_CONFIG) --cflags libavif)
LIBS += $(shell $(PKG_CONFIG) --libs libavif)
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
HEADERS = $(wildcard *.h) $(wildcard ../viewer_linux/*.h) $(wildcard ../include/*.h)
TARGET = ../bin/silver_gallery

all: $(TARGET)

# The vendored single-header libraries, built once. They are ~24k lines that
# never change; recompiling them with every edit was most of the build time.
../lib/exif.o: ../include/easyexif/exif.cpp ../include/easyexif/exif.h
	@mkdir -p ../lib
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

../lib/thirdparty.o: ../src/silver_thirdparty.cpp ../include/stb_image.h ../include/stb_truetype.h ../include/stb_image_resize.h ../include/stb_image_write.h ../include/stb_rect_pack.h ../include/nanosvg.h ../include/nanosvgrast.h
	@mkdir -p ../lib
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) $(INCLUDES) -c $< -o $@

../lib/sqlite3.o: ../include/sqlite3.c
	@mkdir -p ../lib
	$(CC) $(CFLAGS) -I../include -c $< -o $@

$(TARGET): $(SRCS) $(HEADERS) $(VENDOR) ../lib/sqlite3.o ../lib/libglfw3.a
	@mkdir -p ../bin
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) $(INCLUDES) $(SRCS) $(VENDOR) $(LIBS) -o $(TARGET)
	@ln -sf silver_gallery ../bin/silver_gallery_linux
	@echo "Gallery build successful: $(TARGET)"

clean:
	rm -f $(VENDOR) $(TARGET) ../bin/silver_gallery_linux

run: all
	$(TARGET)

.PHONY: all clean run
