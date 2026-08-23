# SilverViewer (Linux / Wayland / OpenGL Core)

A lightweight, ultra-fast image and folder viewer tailored for **Linux / Wayland** using modern **OpenGL 3.3 Core Profile**, with zero editing bloat.

---

## Features

- **Native Wayland & Hardware Accelerated**: Built on modern OpenGL Core Profile with native Wayland windowing.
- **Ultra-Fast & Responsive**: Instant launch, asynchronous folder scanning, and sub-millisecond frame rendering.
- **Cursor-Centered Smooth Zoom & Pan**:
  - Zoom in/out centered directly at the mouse cursor using the mouse wheel.
  - Smooth pan dragging with left or middle mouse button.
  - Instant **1:1 Actual Pixel** mode (`1` key) and **Fit-to-Window** mode (`F` key or double-click).
- **Smart Folder Navigation**:
  - Automatically scans and indexes all supported images in the current folder.
  - Natural numerical sorting (`image1.png`, `image2.png`, `image10.png`).
  - Next / Previous image switching with arrow keys, PageUp/PageDown, or bottom bar controls.
- **Top-Right Metadata Inspector Button**:
  - Interactive **`[ ℹ Metadata ]`** button pinned at the top-right corner.
  - Displays a translucent inspector card with:
    - **File Information**: File name, directory path, file size (KB/MB + bytes), format, modified date.
    - **Image Details**: Dimensions (Width × Height), Megapixels, Aspect Ratio, Color Depth & Channels.
    - **EXIF Camera Data**: Camera Make & Model, Lens, Date/Time Taken, Exposure Time, Aperture (f-stop), ISO, Focal Length, Flash, and GPS coordinates.
- **Inspection Tools**:
  - **Pixel Color Inspector**: Live coordinates and RGB/HEX color values under cursor (`Ctrl+C` to copy HEX).
  - **Pixel-Art Mode**: Toggle between crisp Nearest-Neighbor and smooth Bilinear filtering (`N` key).
  - **Pixel Grid Overlay**: Overlay grid lines at high zoom (`G` key).
  - **Background Options**: Dark Charcoal, Transparency Checkerboard, Pure Black, Pure White (`B` key).

---

## Building & Running

### Build
From the project root directory:
```bash
make
```
This compiles the standalone binary to `bin/silver_viewer`.

### Run
Open an image directly:
```bash
./bin/silver_viewer /path/to/photo.jpg
```
Or open all images in a directory:
```bash
./bin/silver_viewer /path/to/my_folder/
```
Or simply run without arguments to browse images in the current directory:
```bash
./bin/silver_viewer
```

---

## Keyboard & Mouse Shortcuts

| Action | Shortcut / Control |
| :--- | :--- |
| **Next / Previous Image** | `Right Arrow` / `Left Arrow`, `Page Down` / `Page Up`, `D` / `A`, or Mouse Back / Forward |
| **First / Last Image** | `Home` / `End` |
| **Zoom In / Out** | `Mouse Wheel` (centered on cursor), or `+` / `-` |
| **Pan Image** | `Left Click + Drag` or `Middle Click + Drag` |
| **Fit to Window** | `F` key or `Double Click` |
| **1:1 Actual Size (100%)** | `1` key |
| **Rotate 90° Clockwise / CCW** | `R` / `Shift + R` |
| **Toggle Metadata & EXIF Card** | `I` key, `F2`, or Click **Top-Right `[ ℹ Metadata ]` Button** |
| **Toggle Filter (Nearest / Bilinear)** | `N` key |
| **Toggle Pixel Grid** | `G` key (at >= 800% zoom) |
| **Toggle Background Style** | `B` key (Dark, Checkerboard, Black, White) |
| **Copy Pixel Hex Color** | `Ctrl + C` (copies hovered pixel color) |
| **Toggle Fullscreen** | `F11` |
| **Show Help** | `?` or `H` key |
| **Exit / Close Overlays** | `Esc` |
