# SilverGallery & SilverViewer

A blazing-fast, modern photo gallery organizer and hardware-accelerated image viewer designed for **Linux (Wayland & X11)** using modern **OpenGL 3.3 Core Profile** and **SQLite3**.

---

## 🌟 Overview

**SilverGallery** provides an intelligent, timeline-based photo experience similar to modern mobile galleries, coupled with **SilverViewer**, a standalone high-performance image viewer with sub-millisecond response times.

---

## ✨ Features

### 🖼️ SilverGallery (Timeline Photo Organizer)
- **Automatic Multi-Folder Discovery**: Scans and indexes common photo directories (`~/Pictures`, `~/Downloads`, `~/Desktop`, `~/Documents`, `~/camera`, etc.) using high-performance SQLite storage.
- **Mobile-Style Timeline Zoom**: Pinch or use the zoom pill to move between **Day**, **Week**, **Month** and **Year** groupings - the buckets widen as you zoom out, exactly like a phone gallery. Section titles adapt (*"Today"*, *"This Week"*, *"August 2026"*, *"2025"*).
- **Smooth Continuous Scaling**:
  - Touchpad pinch-to-zoom and <kbd>Ctrl</kbd> + 2-finger scroll with sub-pixel interpolation.
  - Interactive bottom-right percentage pill button and popup menu with presets (**XL Icons**, **Large Icons**, **Small Icons**, **Columns**, **List**, **Details**) and a vertical slider.
- **FilePilot Sidebar**:
  - Side-by-side photo inspection panel with instant full-resolution preview.
  - Complete EXIF camera details (Aperture, ISO, Shutter Speed, Focal Length, Camera Model).
  - One-click actions: Open in SilverViewer, Favorite/Star toggle, and Copy Path to clipboard.
- **Favorites & Star System**: Instant photo bookmarking with database persistence.
- **Folder Navigation Tab**: Browse by local folder with photo count badges.
- **Anti-Aliased Thumbnails**: Multi-threaded 512px thumbnail caching with Mitchell downsampling and trilinear OpenGL mipmapping.
- **Keyboard Navigation**: Full arrow-key, PageUp/PageDown, and Home/End auto-scrolling grid navigation.

### 🔍 SilverViewer (Hardware Accelerated Image Viewer)
- **Sub-Millisecond Launch**: Asynchronous multi-threaded image decoding with instant header probing.
- **Smooth Pan & Cursor-Centered Zoom**: Fluid touchpad/mouse wheel zooming centered precisely at cursor position.
- **Metadata Inspector**: Interactive `[ ℹ Metadata ]` overlay detailing file specs, EXIF camera tags, and GPS coordinates.
- **Pixel Inspection Tools**: Live RGB/HEX color picker under the cursor (<kbd>Ctrl+C</kbd> to copy hex) and pixel grid mode.
- **Pixel-Art Mode**: Toggle between smooth bilinear interpolation and nearest-neighbor sampling (<kbd>N</kbd>).

### 🎨 Icons & Vector Rendering
Icons are real SVGs ([Lucide](https://lucide.dev), ISC licensed) embedded in the
binary and rasterized through nanosvg **at the exact pixel size they are drawn**,
cached per (icon, size) in a packed atlas. A 16px icon is rasterized at 16px, so
icons never get resampled and never look soft. Run `./tools/fetch_icons.sh` to
regenerate `include/silver_icons_data.h` from upstream.

### 🧩 Format Support
| Format | Back-end | Notes |
|---|---|---|
| JPEG | libjpeg-turbo (optional) | DCT-scaled thumbnail decoding, ~3x faster than a full decode |
| PNG / BMP / GIF / TGA / PNM | stb_image | built in |
| **WebP** | libwebp (optional) | decoded straight at thumbnail resolution |
| **SVG** | nanosvg (vendored) | rasterized at whatever size the grid needs - always crisp |
| **AVIF** | libavif (optional) | |

Optional back-ends are detected with `pkg-config` at build time. If a library is
missing the build still succeeds, it just falls back to stb_image for that format.
EXIF orientation is applied during decode, so rotated phone shots are upright
everywhere.

### ⚡ Performance Design
- **Parallel indexer**: directory walkers (readdir + stat only) feed metadata inspector
  threads; files already indexed with an unchanged mtime/size are never opened.
  *Measured: 11,322 photos across 626 folders - 2.6s cold index, 0.2s incremental.*
- **Persistent thumbnail cache**: `~/.cache/silver_gallery/thumbs`, keyed by path,
  size tier and source mtime - restarts are instant.
- **Resolution tiers**: thumbnails are decoded at 96/160/256/384/512px to match the
  current grid size instead of always at full size.
- **Viewport-only work**: thumbnail decoding and layout animation run only for the
  tiles actually on screen; scrolling away cancels pending decodes, and off-screen
  tiles snap to their new slot instead of animating.
- **Texture budget**: least-recently-drawn thumbnails are released past ~2000 resident
  textures so a huge library stays inside VRAM.

---

## 🚀 Building & Running

### Requirements
- **GCC / Clang** with C++17 support
- **GLFW3** (`libglfw3-dev` / `glfw-devel`)
- **OpenGL Core** (`mesa-libGL-devel` / `libgl1-mesa-dev`)
- **SQLite3** (`libsqlite3-dev` / `sqlite-devel`)
- **Pthread & dl**

Optional (auto-detected, improves format coverage and speed):
- **libjpeg-turbo** (`libturbojpeg0-dev` / `libjpeg-turbo-devel`)
- **libwebp** (`libwebp-dev` / `libwebp-devel`)
- **libavif** (`libavif-dev` / `libavif-devel`)

### Build Everything
```bash
make all
```
Binaries will be placed in the `bin/` directory:
- `bin/silver_gallery`
- `bin/silver_viewer`

### Run SilverGallery
```bash
make run_gallery
# or
./bin/silver_gallery
```

### Run SilverViewer
```bash
./bin/silver_viewer /path/to/image.jpg
# or open an entire directory
./bin/silver_viewer /path/to/folder/
```

---

## ⌨️ Shortcuts & Navigation

### SilverGallery Shortcuts
| Action | Key / Gesture |
| :--- | :--- |
| **Smooth Grid Scaling** | Touchpad Pinch / <kbd>Ctrl</kbd> + Mouse Wheel / Popup Slider |
| **Zoom In / Out** | <kbd>+</kbd> / <kbd>-</kbd> |
| **Reset Grid View** | <kbd>Ctrl</kbd> + <kbd>0</kbd> |
| **Navigate Photos** | <kbd>←</kbd> / <kbd>→</kbd> / <kbd>↑</kbd> / <kbd>↓</kbd> |
| **Page Scroll** | <kbd>Page Up</kbd> / <kbd>Page Down</kbd> |
| **Jump to Top / Bottom** | <kbd>Home</kbd> / <kbd>End</kbd> |
| **Open Fullscreen** | <kbd>Enter</kbd> or <kbd>F</kbd> |
| **Toggle Favorite Star** | <kbd>Space</kbd> or Star icon |
| **Refresh / Re-scan** | <kbd>F5</kbd> or <kbd>Ctrl</kbd> + <kbd>R</kbd> |
| **Toggle Dark / Light Theme** | <kbd>T</kbd> |
| **Close Sidebar / Dismiss Popup** | <kbd>Esc</kbd> |

### SilverViewer Shortcuts
| Action | Key / Gesture |
| :--- | :--- |
| **Next / Previous Image** | <kbd>→</kbd> / <kbd>←</kbd>, <kbd>Page Down</kbd> / <kbd>Page Up</kbd>, or Mouse Back/Forward |
| **Zoom In / Out** | Mouse Wheel (cursor-centered), or <kbd>+</kbd> / <kbd>-</kbd> |
| **Pan Image** | <kbd>Left Click + Drag</kbd> or <kbd>Middle Click + Drag</kbd> |
| **Fit to Window** | <kbd>F</kbd> or Double Click |
| **1:1 Actual Size (100%)** | <kbd>1</kbd> |
| **Rotate 90°** | <kbd>R</kbd> / <kbd>Shift</kbd> + <kbd>R</kbd> |
| **Toggle Metadata Card** | <kbd>I</kbd>, <kbd>F2</kbd>, or `[ ℹ Metadata ]` button |
| **Toggle Nearest / Bilinear Filter** | <kbd>N</kbd> |
| **Toggle Pixel Grid** | <kbd>G</kbd> (at >= 800% zoom) |
| **Toggle Background Style** | <kbd>B</kbd> (Dark, Checkerboard, Black, White) |
| **Copy Pixel Hex** | <kbd>Ctrl</kbd> + <kbd>C</kbd> |
| **Toggle Fullscreen** | <kbd>F11</kbd> |
| **Exit** | <kbd>Esc</kbd> |

---

## 📄 License
Released under the MIT License.
