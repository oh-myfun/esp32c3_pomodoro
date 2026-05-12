#!/bin/bash
# Generate LVGL font files from Maple Mono CN + Noto Emoji
# Prerequisites: node.js + npm install (lv_font_conv)

set -e
cd "$(dirname "$0")/.."

# Generate symbol lists first
echo "Generating symbol lists..."
python fonts/gen_gb2312_ranges.py
python fonts/gen_emoji_symbols.py
echo ""

# Run Node.js font generator (bypasses shell command line length limits)
node fonts/gen_fonts_node.js
