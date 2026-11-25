#!/usr/bin/env bash
# Downloads Kenney's Furniture Kit (CC0) into assets/kit.
# If you'd rather not download at build time, unzip the kit into assets/kit
# yourself and commit it - this script will then do nothing.
set -u
mkdir -p assets/kit assets/dl
if find assets/kit -iname '*.obj' | grep -q .; then
  echo "Kenney kit already present in assets/kit"; exit 0
fi
UA="Mozilla/5.0 (X11; Linux x86_64) PocketLife-DS-build"
urls=()
page=$(curl -fsSL -A "$UA" https://kenney.nl/assets/furniture-kit 2>/dev/null || true)
u=$(printf '%s' "$page" | grep -oE 'https://kenney\.nl/media/pages/assets/furniture-kit/[^"'"'"' ]+\.zip' | head -1)
[ -n "$u" ] && urls+=("$u")
urls+=("https://opengameart.org/sites/default/files/kenney_furniturePack.zip"
       "https://opengameart.org/sites/default/files/kenney_furniture-kit.zip"
       "https://opengameart.org/sites/default/files/furnitureKit.zip")
for u in "${urls[@]}"; do
  echo "trying $u"
  rm -f assets/dl/kit.zip
  if curl -fsSL -A "$UA" -o assets/dl/kit.zip "$u" && unzip -tq assets/dl/kit.zip >/dev/null 2>&1; then
    unzip -qo assets/dl/kit.zip -d assets/kit
    # some packs ship zips inside the zip
    find assets/kit -iname '*.zip' -exec unzip -qo {} -d assets/kit \; 2>/dev/null || true
    n=$(find assets/kit -iname '*.obj' | wc -l)
    echo "got the kit from $u ($n OBJ models)"
    [ "$n" -gt 0 ] && exit 0
  fi
done
echo "::warning::Could not download the Kenney Furniture Kit - the ROM will use placeholder boxes. Put the kit in assets/kit to fix."
exit 0
