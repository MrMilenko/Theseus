#!/bin/bash
# Bundle a Linux desktop build into a single AppImage.
#
# Built on Ubuntu, run on Debian and Arch, so the exclusion list below only
# names libraries present on both. Get that wrong and it fails on one distro
# while working fine on the other: libselinux is Debian only, and libXpresent
# comes in with mpv rather than the base X stack.
set -euo pipefail

SRC="${1:-$HOME/builds/theseus/desktop}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="$(uname -m)"
OUT="${2:-$REPO/UIX-Desktop-$ARCH.AppImage}"
APP="$REPO/AppDir"

command -v patchelf >/dev/null || { echo "need patchelf"; exit 1; }

rm -rf "$APP"
mkdir -p "$APP/usr/bin/Data" "$APP/usr/lib"

# Payload sits beside the binary: Plat_ShippedDir() is the exe dir, and first
# run seeds the user directory from here.
cp "$SRC/theseus" "$APP/usr/bin/"
cp -r "$REPO/Data/shaders" "$APP/usr/bin/Data/shaders"
for d in "$REPO"/Data/*/; do
  n="$(basename "$d")"
  [ "$n" = "shaders" ] && continue
  cp -r "$d" "$APP/usr/bin/Data/$n"
done
[ -d "$REPO/Configs" ] && cp -r "$REPO/Configs" "$APP/usr/bin/Configs"
[ -d "$REPO/Library" ] && cp -r "$REPO/Library" "$APP/usr/bin/Library"
[ -d "$SRC/lib" ] && cp -a "$SRC/lib/." "$APP/usr/lib/" || true

KEEP_HOST="ld-linux|libc\.so|libm\.so|libdl\.so|libpthread|librt\.so|libgcc_s|libstdc\+\+|libGL|libEGL|libGLX|libGLdispatch|libOpenGL|libvulkan|libdrm|libX11|libXext\.|libXau|libXdmcp|libxcb|libwayland|libsystemd|libdbus|libudev|libasound|libpulse"

walk() {
  ldd "$1" 2>/dev/null | awk '{print $1, $3}' | while read -r so path; do
    case "$so" in */*|linux-vdso*|"") continue;; esac
    [ -f "${path:-}" ] || continue
    echo "$so $path"
  done
}
for _ in 1 2 3 4 5; do
  { walk "$APP/usr/bin/theseus"
    for l in "$APP"/usr/lib/*.so*; do [ -f "$l" ] && walk "$l"; done
  } | sort -u | while read -r so path; do
      echo "$so" | grep -qE "$KEEP_HOST" && continue
      [ -f "$APP/usr/lib/$so" ] || cp -L "$path" "$APP/usr/lib/$so"
    done
done

# DT_RUNPATH is not transitive, so each bundled lib needs its own $ORIGIN or
# it cannot find the sibling sitting next to it.
for l in "$APP"/usr/lib/*.so*; do
  [ -f "$l" ] && patchelf --set-rpath '$ORIGIN' "$l" 2>/dev/null || true
done
patchelf --set-rpath '$ORIGIN/../lib' "$APP/usr/bin/theseus"

cat > "$APP/AppRun" <<'EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"
# Children inherit LD_LIBRARY_PATH, and a host /bin/sh that picks up our
# bundled readline dies on an undefined symbol before it can exec steam.
# Stash the caller's value so the launcher can put it back when it spawns.
export THESEUS_HOST_LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH:-}"
cd "${HERE}/usr/bin"
exec "${HERE}/usr/bin/theseus" "$@"
EOF
chmod +x "$APP/AppRun"

cat > "$APP/theseus.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=UIX Desktop
Comment=Xbox dashboard launcher and media center
Exec=AppRun
Icon=theseus
Categories=Game;
Terminal=false
EOF
cp "$REPO/build/icon.png" "$APP/theseus.png" 2>/dev/null || \
  cp "$REPO/Configs/steamlogo.png" "$APP/theseus.png"

echo "AppDir: $(ls "$APP/usr/lib" | wc -l) bundled libs, $(du -sh "$APP" | cut -f1)"

APPIMAGETOOL="${APPIMAGETOOL:-}"
if [ -z "$APPIMAGETOOL" ]; then
  APPIMAGETOOL=/tmp/appimagetool
  wget -q https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$ARCH.AppImage -O "$APPIMAGETOOL" ||
  wget -q https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-$ARCH.AppImage -O "$APPIMAGETOOL"
  chmod +x "$APPIMAGETOOL"
fi
ARCH="$ARCH" APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGETOOL" "$APP" "$OUT"
echo "built: $OUT ($(du -h "$OUT" | cut -f1))"
