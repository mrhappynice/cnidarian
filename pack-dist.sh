#!/usr/bin/env bash
set -euo pipefail

DIST_DIR="dist"

echo "⚙️  Creating $DIST_DIR/ ..."

# Clean and recreate dist
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/pkg"

# --- Copy HTML + JS entry from web/ ---

if [ ! -f "web/index.html" ] || [ ! -f "web/main.js" ]; then
  echo "❌ Expected web/index.html and web/main.js next to this script."
  exit 1
fi

cp -v web/index.html "$DIST_DIR/index.html"
cp -v web/main.js    "$DIST_DIR/main.js"

# --- Copy wasm + its JS loader from pkg/ ---

if ! ls pkg/*.js >/dev/null 2>&1; then
  echo "❌ No .js files found in pkg/ (expected livebg.js)."
  exit 1
fi

if ! ls pkg/*.wasm >/dev/null 2>&1; then
  echo "❌ No .wasm files found in pkg/ (expected livebg.wasm)."
  exit 1
fi

cp -v pkg/*.js   "$DIST_DIR/pkg/"
cp -v pkg/*.wasm "$DIST_DIR/pkg/"

echo
echo "✅ Packed static site into $DIST_DIR/:"
find "$DIST_DIR" -maxdepth 3 -type f -print | sed 's/^/  /'
echo
echo "You can point Netlify (or any static host) at $DIST_DIR/."

