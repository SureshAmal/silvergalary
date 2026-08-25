#!/usr/bin/env bash
# Regenerate include/silver_icons_data.h from upstream Lucide SVGs.
#
# Lucide (https://lucide.dev) is ISC licensed. Run from the repo root:
#   ./tools/fetch_icons.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# enum name : lucide file : filled(0/1)
MAP="
ICON_FIT:maximize:0
ICON_1TO1:ratio:0
ICON_ROTATE:rotate-cw:0
ICON_GRID:grid-3x3:0
ICON_GRID_2X2:grid-2x2:0
ICON_GRID_4X4:grid-4x4:0
ICON_GRID_CHECK:square-check:0
ICON_TARGET:target:0
ICON_INFO:info:0
ICON_THEME_DARK:moon:0
ICON_THEME_LIGHT:sun:0
ICON_CLOSE:x:0
ICON_CHEVRON_LEFT:chevron-left:0
ICON_CHEVRON_RIGHT:chevron-right:0
ICON_CHEVRON_DOWN:chevron-down:0
ICON_CHEVRON_UP:chevron-up:0
ICON_DOC:file-text:0
ICON_CAMERA:camera:0
ICON_CALENDAR:calendar:0
ICON_DIMENSIONS:ruler:0
ICON_LOCATION:map-pin:0
ICON_CHECK:check:0
ICON_ZOOM_PLUS:zoom-in:0
ICON_FOLDER:folder:0
ICON_SEARCH:search:0
ICON_HEART:heart:0
ICON_HEART_FILLED:heart:1
ICON_STAR:star:0
ICON_STAR_FILLED:star:1
ICON_REFRESH:refresh-cw:0
ICON_PHOTO:image:0
ICON_COPY:copy:0
ICON_EXTERNAL_LINK:external-link:0
ICON_ARROW_LEFT:arrow-left:0
ICON_ARROW_RIGHT:arrow-right:0
ICON_LIST:list:0
ICON_COLUMNS:columns-3:0
ICON_SLIDERS:sliders-horizontal:0
"

BASE="https://raw.githubusercontent.com/lucide-icons/lucide/main/icons"
for entry in $MAP; do
    name="${entry#*:}"; name="${name%:*}"
    [ -f "$WORK/$name.svg" ] && continue
    curl -sSfL -o "$WORK/$name.svg" "$BASE/$name.svg"
done

MAP="$MAP" WORK="$WORK" python3 - "$REPO_ROOT/include/silver_icons_data.h" <<'PYEOF'
import os, re, sys
work = os.environ["WORK"]
out = [
 '#pragma once', '',
 '// ' + '-' * 77,
 '// Icon artwork - Lucide (https://lucide.dev), ISC licensed.',
 '//',
 '// Source SVGs are embedded verbatim so there is no runtime asset dependency.',
 '// Two edits are applied to each: currentColor becomes #FFFFFF (the UI shader',
 '// tints the icon through the vertex colour, and nanosvg does not understand',
 '// currentColor), and the filled variants get fill="#FFFFFF" on the root.',
 '//',
 '// Generated - do not hand-edit. Run tools/fetch_icons.sh to regenerate.',
 '// ' + '-' * 77, '',
 'struct IconSvg {', '    const char* name;', '    const char* svg;', '};', '',
 'static const IconSvg kIconSvgs[] = {',
]
for entry in os.environ["MAP"].split():
    enum_name, file_name, filled = entry.split(":")
    svg = open(os.path.join(work, file_name + ".svg")).read()
    svg = svg.replace("currentColor", "#FFFFFF")
    if filled == "1":
        svg = svg.replace('fill="none"', 'fill="#FFFFFF"')
    svg = re.sub(r"\s*\n\s*", " ", svg).strip()
    out.append('    { "%s", R"SVG(%s)SVG" },' % (enum_name, svg))
out += ['};', '', 'static const int kIconSvgCount = (int)(sizeof(kIconSvgs) / sizeof(kIconSvgs[0]));', '']
open(sys.argv[1], "w").write("\n".join(out))
print("wrote", sys.argv[1])
PYEOF
