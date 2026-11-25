#!/usr/bin/env bash
# Builds pocketlife.nds. Tries our Makefile first; if the installed libnds is a
# newer/older flavour that our Makefile doesn't match, falls back to the
# official devkitPro arm9 template Makefile.
set -u
if make -j2; then exit 0; fi
echo "::warning::Project Makefile failed, retrying with the devkitPro arm9 template"
make clean >/dev/null 2>&1 || true
TPL=""
for p in /opt/devkitpro/examples/nds/templates/arm9/Makefile; do [ -f "$p" ] && TPL="$p"; done
if [ -z "$TPL" ]; then
  git clone --depth 1 https://github.com/devkitPro/nds-examples /tmp/nds-examples && TPL=/tmp/nds-examples/templates/arm9/Makefile
fi
cp "$TPL" Makefile.dkp
sed -i -E 's/^(TARGET[[:space:]]*:=).*/\1 pocketlife/' Makefile.dkp
sed -i -E 's/^(GAME_TITLE[[:space:]]*:=).*/\1 PocketLife/' Makefile.dkp
# add libm, and libfat if this toolchain has it
EXTRA="-lm"
if find /opt/devkitpro -name 'libfat.a' 2>/dev/null | grep -q .; then EXTRA="-lfat $EXTRA"; fi
sed -i -E "s/^(LIBS[[:space:]]*:=)(.*)$/\1 $EXTRA \2 -lm/" Makefile.dkp
sed -i -E 's/-std=[a-z0-9+]+//' Makefile.dkp
grep -nE '^(TARGET|LIBS|SOURCES|INCLUDES)' Makefile.dkp
make -f Makefile.dkp -j2
