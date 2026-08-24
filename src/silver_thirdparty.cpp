// -----------------------------------------------------------------------------
// The single translation unit that compiles the vendored single-header
// libraries.
//
// These are ~24,000 lines that never change, and they used to be recompiled into
// every binary on every build - roughly 60% of the build time spent on code
// nobody had touched. Building them once into an object file makes an
// incremental rebuild of our own ~15,000 lines dramatically faster.
//
// Include order matters and is not arbitrary:
//
//   * stb_rect_pack MUST precede stb_truetype. Without STB_RECT_PACK_VERSION
//     already defined, stb_truetype compiles its own private stbrp_* stub and
//     the two definitions collide at link time.
//   * nanosvg keeps its implementation *inside* its include guard, so this must
//     be the first inclusion of it in this translation unit.
// -----------------------------------------------------------------------------

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// Must match the flag the rest of the codebase compiles nanosvg with, or the
// colour keyword table differs between declaration and definition.
#define NANOSVG_ALL_COLOR_KEYWORDS
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
