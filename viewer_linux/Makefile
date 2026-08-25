CXX ?= g++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wno-unused-function -Wno-unused-result
# Suppress warnings originating in vendored stb/NanoSVG only. Application
# sources retain the normal warning policy.
VENDOR_WARNINGS = -Wno-sign-compare -Wno-unused-but-set-variable \
	-Wno-maybe-uninitialized -Wno-stringop-overflow
INCLUDES = -I../include -I../include/easyexif -I../include/GLFW
LIBS = ../lib/libglfw3.a -lGL -lm -lpthread -ldl

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
# The runtime and development packages are split on Fedora. We vendor the WebP
# headers, so a present ABI-compatible runtime is sufficient for decoding.
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
HEADERS = $(wildcard *.h) $(wildcard ../include/*.h)
TARGET = ../bin/silver_viewer

all: $(TARGET)

# The vendored single-header libraries, built once. They are ~24k lines that
# never change; recompiling them with every edit was most of the build time.
../lib/exif.o: ../include/easyexif/exif.cpp ../include/easyexif/exif.h
	@mkdir -p ../lib
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

../lib/thirdparty.o: ../src/silver_thirdparty.cpp ../include/stb_image.h ../include/stb_truetype.h ../include/stb_image_resize.h ../include/stb_image_write.h ../include/stb_rect_pack.h ../include/nanosvg.h ../include/nanosvgrast.h
	@mkdir -p ../lib
	$(CXX) $(CXXFLAGS) $(VENDOR_WARNINGS) $(PKG_CFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET): $(SRCS) $(HEADERS) $(VENDOR) ../lib/libglfw3.a
	@mkdir -p ../bin
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) $(INCLUDES) $(SRCS) $(VENDOR) $(LIBS) -o $(TARGET)
	@ln -sf silver_viewer ../bin/silver_viewer_linux
	@echo "Build successful: $(TARGET)"

clean:
	rm -f $(VENDOR) $(TARGET) ../bin/silver_viewer_linux

run: all
	$(TARGET)

.PHONY: all clean run
