#pragma once

// Stable fallback values and safety limits. User-facing quality and layout
// preferences belong in config/silver.json.
namespace silver {
namespace defaults {
inline constexpr int galleryWindowWidth = 1200;
inline constexpr int galleryWindowHeight = 800;
inline constexpr int viewerWindowWidth = 1280;
inline constexpr int viewerWindowHeight = 800;
inline constexpr int msaaSamples = 4;
inline constexpr float baseFontPoints = 15.0f;
inline constexpr int thumbnailEdge = 512;
inline constexpr int fullResolutionDecode = 0;
inline constexpr float minPixelScale = 0.5f;
}
namespace limits {
inline constexpr int maxMsaaSamples = 16;
inline constexpr int maxIconRasterPixels = 512;
inline constexpr int minCornerSegments = 8;
inline constexpr int maxCornerSegments = 24;
}
}
