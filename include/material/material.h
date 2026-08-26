#pragma once

// Material component library - umbrella header.
//
//   material/tokens.h     design tokens
//   material/theme.h      theme definitions + registry
//   material/painter.h    the renderer contract, Rect, Interaction
//   material/state.h      state layers, role pairs, elevation
//   material/button.h     filled / tonal / outlined / text / elevated / icon
//   material/segmented.h  segmented button
//   material/chip.h       assist / filter / input / suggestion
//   material/list.h       list items, dividers, data tables
//   material/surface.h    card / menu / dialog / app bar / sheet / scrim
//   material/anim.h       per-widget animation state, spring-driven
//   material/selection.h  switch / checkbox / radio
//   material/slider.h     sliders, value indicators
//   material/textfield.h  outlined / filled / rounded fields
//   material/card.h       elevated / filled / outlined cards
//   material/fab.h        FABs and extended FABs
//   material/toolbar.h    toolbars, button groups, split buttons
//   material/badge.h      corner and inline badges
//   material/progress.h   linear and circular progress
//
// Header-only. Depends on nothing outside this directory. Every draw function is
// templated on a Painter (see painter.h), so the library carries no knowledge of
// the host renderer, and swapping themes is swapping the ThemeTokens passed in.

#include "tokens.h"
#include "theme.h"
#include "painter.h"
#include "state.h"
#include "button.h"
#include "segmented.h"
#include "chip.h"
#include "list.h"
#include "surface.h"
#include "anim.h"
#include "selection.h"
#include "slider.h"
#include "textfield.h"
#include "card.h"
#include "fab.h"
#include "toolbar.h"
#include "badge.h"
#include "progress.h"
