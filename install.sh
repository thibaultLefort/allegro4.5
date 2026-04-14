#!/usr/bin/env bash
# install.sh
# Installs Allegro 4.4.3.1 patched for macOS arm64 (Apple Silicon).
# Usage:  ./install.sh [PREFIX]
#         PREFIX defaults to /usr/local
#
# Requirements: Xcode Command Line Tools, cmake (brew install cmake)
# Optional:     XQuartz for X11 support (brew install --cask xquartz)
#
# After install, build your project with:
#   gcc main.c $(allegro-config --libs) -o myprogram

set -euo pipefail

# ── Config ────────────────────────────────────────────────────────────────────
ALLEGRO_VERSION="4.4.3.1"
TARBALL="allegro-${ALLEGRO_VERSION}.tar.gz"
# Official release hosted on GitHub under the liballeg/allegro5 repo
DOWNLOAD_URLS=(
    "https://github.com/liballeg/allegro5/releases/download/${ALLEGRO_VERSION}/${TARBALL}"
)
SRCDIR="/tmp/allegro-${ALLEGRO_VERSION}"
BUILDDIR="${SRCDIR}/build-arm64"
PREFIX="${1:-/usr/local}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PATCH="${SCRIPT_DIR}/allegro4-macos-arm.patch"
LOCAL_SRCDIR="${SCRIPT_DIR}/allegro-${ALLEGRO_VERSION}"
NCPU="$(sysctl -n hw.ncpu)"

# ── Helpers ───────────────────────────────────────────────────────────────────
info()  { printf '\033[1;34m[allegro4]\033[0m %s\n' "$*"; }
ok()    { printf '\033[1;32m[allegro4]\033[0m %s\n' "$*"; }
die()   { printf '\033[1;31m[allegro4] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

# ── Pre-flight checks ─────────────────────────────────────────────────────────
[[ "$(uname -s)" == "Darwin" ]] || die "This script is for macOS only."

ARCH="$(uname -m)"
[[ "$ARCH" == "arm64" ]] || die "This script targets arm64 (Apple Silicon). Got: $ARCH"

command -v cmake  >/dev/null 2>&1 || die "cmake not found. Run: brew install cmake"
command -v patch  >/dev/null 2>&1 || die "patch not found. Install Xcode Command Line Tools: xcode-select --install"
command -v cc     >/dev/null 2>&1 || die "C compiler not found. Install Xcode Command Line Tools: xcode-select --install"

[[ -f "$PATCH" ]] || die "Patch file not found: $PATCH"

SDK="$(xcrun --sdk macosx --show-sdk-path 2>/dev/null)" \
    || die "Xcode SDK not found. Run: xcode-select --install"

# ── Detect X11 (XQuartz) ──────────────────────────────────────────────────────
WANT_X11=OFF
X11_ARGS=()
HOMEBREW_PREFIX="${HOMEBREW_PREFIX:-/opt/homebrew}"

if [[ -f "${HOMEBREW_PREFIX}/lib/libX11.dylib" ]]; then
    info "X11 found at ${HOMEBREW_PREFIX} — enabling X11 support"
    WANT_X11=ON
    X11_ARGS=(
        "-DX11_INCLUDE_DIR=${HOMEBREW_PREFIX}/include"
        "-DX11_X11_LIB=${HOMEBREW_PREFIX}/lib/libX11.dylib"
        "-DX11_Xext_LIB=${HOMEBREW_PREFIX}/lib/libXext.dylib"
    )
elif [[ -f "/opt/X11/lib/libX11.dylib" ]]; then
    info "X11 found at /opt/X11 — enabling X11 support"
    WANT_X11=ON
    X11_ARGS=(
        "-DX11_INCLUDE_DIR=/opt/X11/include"
        "-DX11_X11_LIB=/opt/X11/lib/libX11.dylib"
        "-DX11_Xext_LIB=/opt/X11/lib/libXext.dylib"
    )
else
    info "X11 not found — building without X11 (windowed Cocoa mode only)"
fi

# ── Download ──────────────────────────────────────────────────────────────────
TARBALL_PATH="/tmp/${TARBALL}"

if [[ -d "$LOCAL_SRCDIR" ]]; then
    info "Using vendored patched source at ${LOCAL_SRCDIR}"
    # /tmp/allegro-* may be owned by root from a previous sudo-install; use sudo if needed.
    if [[ -d "$SRCDIR" ]] && [[ ! -w "$SRCDIR" ]]; then
        sudo rm -rf "$SRCDIR"
    else
        rm -rf "$SRCDIR"
    fi
    cp -R "$LOCAL_SRCDIR" "$SRCDIR"
elif [[ -d "$SRCDIR" ]]; then
    info "Source already present at ${SRCDIR}, skipping download."
else
    info "Downloading Allegro ${ALLEGRO_VERSION}..."
    downloaded=0
    for url in "${DOWNLOAD_URLS[@]}"; do
        info "  trying: $url"
        if command -v curl >/dev/null 2>&1; then
            curl -L --fail --progress-bar -o "$TARBALL_PATH" "$url" 2>/dev/null || continue
        elif command -v wget >/dev/null 2>&1; then
            wget -q --show-progress -O "$TARBALL_PATH" "$url" 2>/dev/null || continue
        else
            die "Neither curl nor wget found."
        fi
        # Verify it's actually a gzip archive, not an HTML error page
        if file "$TARBALL_PATH" | grep -q "gzip\|tar"; then
            downloaded=1
            break
        else
            info "  got HTML/invalid response, trying next URL..."
            rm -f "$TARBALL_PATH"
        fi
    done
    [[ $downloaded -eq 1 ]] || die "All download URLs failed. Download manually:
  https://sourceforge.net/projects/alleg/files/allegro/${ALLEGRO_VERSION}/${TARBALL}/download
Then place it at: ${TARBALL_PATH}"

    info "Extracting..."
    # GitHub archive has an extra top-level directory named allegro-VERSION
    # SourceForge archive has allegro-VERSION directly — both work with --strip 1 into SRCDIR
    mkdir -p "$SRCDIR"
    tar -xzf "$TARBALL_PATH" -C "$SRCDIR" --strip-components=1
    rm -f "$TARBALL_PATH"
fi

# ── Apply patch ───────────────────────────────────────────────────────────────
PATCH_STAMP="${SRCDIR}/.arm64_patch_applied"

if [[ -d "$LOCAL_SRCDIR" ]]; then
    info "Using vendored source tree, skipping patch application."
elif [[ -f "$PATCH_STAMP" ]]; then
    info "Patch already applied, skipping."
else
    info "Applying ARM64 patch..."
    # The patch was created with `diff file.orig file`, so the --- side has
    # a .orig suffix that doesn't exist in a fresh checkout.  Strip it so
    # patch targets the real file names.
    cd "$SRCDIR"
    sed 's|^\(--- [^[:space:]]*\)\.orig[[:space:]]|\1\t|' "$PATCH" | patch -p0 --forward
    touch "$PATCH_STAMP"
    ok "Patch applied."
fi

# ── CMake configure ───────────────────────────────────────────────────────────
info "Configuring (cmake)..."
cmake -S "$SRCDIR" -B "$BUILDDIR" -Wno-dev \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_SYSROOT="$SDK" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_PREFIX_PATH="${HOMEBREW_PREFIX}" \
    "${X11_ARGS[@]+"${X11_ARGS[@]}"}" \
    -DWANT_LEGACY_MACOSX=OFF \
    -DMAGIC_MAIN=OFF \
    -DWANT_EXAMPLES=OFF \
    -DWANT_TOOLS=OFF \
    -DWANT_TESTS=OFF \
    -DWANT_ALLEGROGL=OFF \
    -DWANT_LOADPNG=OFF \
    -DWANT_LOGG=OFF \
    -DWANT_JPGALLEG=OFF \
    -DWANT_FRAMEWORKS=OFF \
    -DWANT_MODULES=OFF \
    -DWANT_OSS=OFF \
    -DWANT_ALSA=OFF \
    -DWANT_JACK=OFF \
    -DWANT_SGIAUDIO=OFF

# ── Build ─────────────────────────────────────────────────────────────────────
info "Building with ${NCPU} jobs..."
cmake --build "$BUILDDIR" --target allegro alleg-main -j"$NCPU"

# ── Install ───────────────────────────────────────────────────────────────────
# cmake --install --component doesn't work with Allegro 4's CMake setup
# (no install components are defined), so we copy the built files directly.
info "Installing to ${PREFIX} (may ask for sudo password)..."

_install() {
    if [[ -w "$PREFIX" ]]; then
        "$@"
    else
        sudo "$@"
    fi
}

_install install -d "$PREFIX/lib" "$PREFIX/include" "$PREFIX/bin"

# dylib + symlinks
_install install -m 755 "$BUILDDIR/lib/liballeg.4.4.3.dylib" "$PREFIX/lib/"
_install ln -sf liballeg.4.4.3.dylib "$PREFIX/lib/liballeg.4.4.dylib"
_install ln -sf liballeg.4.4.dylib   "$PREFIX/lib/liballeg.dylib"

# static main wrapper
[[ -f "$BUILDDIR/lib/liballeg-main.a" ]] && \
    _install install -m 644 "$BUILDDIR/lib/liballeg-main.a" "$PREFIX/lib/"

# allegrogl (optional)
if [[ -f "$BUILDDIR/lib/liballeggl.4.4.3.dylib" ]]; then
    _install install -m 755 "$BUILDDIR/lib/liballeggl.4.4.3.dylib" "$PREFIX/lib/"
    _install ln -sf liballeggl.4.4.3.dylib "$PREFIX/lib/liballeggl.4.4.dylib"
    _install ln -sf liballeggl.4.4.dylib   "$PREFIX/lib/liballeggl.dylib"
fi

# headers
_install cp -R "$SRCDIR/include/." "$PREFIX/include/"

# allegro-config script
if [[ -f "$BUILDDIR/allegro-config" ]]; then
    _install install -m 755 "$BUILDDIR/allegro-config" "$PREFIX/bin/allegro-config"
elif [[ -f "$SRCDIR/allegro-config.in" ]]; then
    # generate from template if cmake didn't produce it
    sed -e "s|@prefix@|$PREFIX|g" \
        -e "s|@exec_prefix@|\${prefix}|g" \
        -e "s|@libdir@|\${exec_prefix}/lib|g" \
        -e "s|@includedir@|\${prefix}/include|g" \
        -e "s|@VERSION@|$ALLEGRO_VERSION|g" \
        "$SRCDIR/allegro-config.in" > /tmp/allegro-config
    _install install -m 755 /tmp/allegro-config "$PREFIX/bin/allegro-config"
fi

# ── Done ──────────────────────────────────────────────────────────────────────
ok "Allegro ${ALLEGRO_VERSION} installed to ${PREFIX}"
echo ""
echo "Build your project with:"
echo "  gcc main.c \$(allegro-config --libs) -o myprogram"
echo "  ./myprogram"
echo ""

if ! command -v allegro-config >/dev/null 2>&1; then
    echo "NOTE: allegro-config is not on your PATH. Add ${PREFIX}/bin to PATH:"
    echo "  export PATH=\"${PREFIX}/bin:\$PATH\""
fi
