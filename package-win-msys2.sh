#!/usr/bin/env bash
#
# Build DXVK for Windows using MSYS2 MinGW toolchains.
# Run from an MSYS2 MINGW64 shell:
#
#   ./package-win-msys2.sh                  # release x64+x32
#   ./package-win-msys2.sh --debug          # debug x64+x32
#   ./package-win-msys2.sh --64-only        # release x64 only
#   ./package-win-msys2.sh --debug --64-only
#
# Output:
#   build/release/x64/*.dll  build/release/x32/*.dll
#   build/debug/x64/*.dll    build/debug/x32/*.dll
#

set -e

DXVK_SRC_DIR=$(dirname "$(readlink -f "$0")")
DXVK_TMPDIR="$DXVK_SRC_DIR/.buildtmp"

opt_debug=0
opt_buildid=false
opt_64_only=0
opt_32_only=0

while [ $# -gt 0 ]; do
  case "$1" in
  "--debug")    opt_debug=1 ;;
  "--build-id") opt_buildid=true ;;
  "--64-only")  opt_64_only=1 ;;
  "--32-only")  opt_32_only=1 ;;
  *)
    echo "Usage: $0 [--debug] [--64-only] [--32-only] [--build-id]" >&2
    exit 1
  esac
  shift
done

if [ $opt_debug -eq 1 ]; then
  BUILD_TYPE="debug"
  MESON_TYPE="debug"
  STRIP_FLAG=""
else
  BUILD_TYPE="release"
  MESON_TYPE="release"
  STRIP_FLAG="--strip"
fi

DXVK_OUT_DIR="$DXVK_SRC_DIR/build/$BUILD_TYPE"

# --- Dependency check ---
check_deps() {
  local missing=()
  local deps_64=(
    mingw-w64-x86_64-gcc
    mingw-w64-x86_64-meson
    mingw-w64-x86_64-ninja
    mingw-w64-x86_64-glslang
  )
  local deps_32=(
    mingw-w64-i686-gcc
    mingw-w64-i686-binutils
    mingw-w64-i686-crt-git
    mingw-w64-i686-headers-git
    mingw-w64-i686-winpthreads
  )

  if [ $opt_32_only -eq 0 ]; then
    for pkg in "${deps_64[@]}"; do
      pacman -Qi "$pkg" &>/dev/null || missing+=("$pkg")
    done
  fi
  if [ $opt_64_only -eq 0 ]; then
    for pkg in "${deps_32[@]}"; do
      pacman -Qi "$pkg" &>/dev/null || missing+=("$pkg")
    done
  fi

  if [ ${#missing[@]} -gt 0 ]; then
    echo "Installing missing packages: ${missing[*]}"
    pacman -S --noconfirm "${missing[@]}"
  fi
}

# --- Generate cross-file for given arch ---
generate_crossfile() {
  local bits="$1"
  local file="$2"

  if [ "$bits" = "64" ]; then
    local prefix="x86_64-w64-mingw32"
    local cpu_family="x86_64"
    local cpu="x86_64"
    local mingw_bindir="/mingw64/bin"
  else
    local prefix="i686-w64-mingw32"
    local cpu_family="x86"
    local cpu="i686"
    local mingw_bindir="/mingw32/bin"
  fi

  local win_bindir
  win_bindir=$(cygpath -w "$mingw_bindir")

  cat > "$file" <<EOF
[binaries]
c = '${prefix}-gcc'
cpp = '${prefix}-g++'
ar = '${prefix}-gcc-ar'
strip = '${win_bindir}\\strip.exe'
windres = '${win_bindir}\\windres.exe'

[properties]
needs_exe_wrapper = true

[host_machine]
system = 'windows'
cpu_family = '${cpu_family}'
cpu = '${cpu}'
endian = 'little'
EOF
}

# --- Build one arch ---
build_arch() {
  local bits="$1"
  local outdir="$DXVK_OUT_DIR/x${bits}"
  local builddir="$DXVK_TMPDIR/${BUILD_TYPE}-${bits}"

  cd "$DXVK_SRC_DIR"
  export PATH="/mingw64/bin:/mingw32/bin:$PATH"

  local crossfile="$DXVK_TMPDIR/crossfile-${bits}.txt"
  mkdir -p "$DXVK_TMPDIR"
  generate_crossfile "$bits" "$crossfile"

  meson setup --cross-file "$crossfile"        \
        --buildtype "$MESON_TYPE"              \
        $STRIP_FLAG                            \
        -Db_ndebug=if-release                  \
        -Dbuild_id=$opt_buildid                \
        "$builddir"

  cd "$builddir"
  ninja

  mkdir -p "$outdir"
  find . -name '*.dll' ! -name '*.dll.a' -exec cp {} "$outdir/" \;
}

# --- Cleanup temp ---
cleanup() {
  rm -rf "$DXVK_TMPDIR"
}

# --- Main ---
echo "=== DXVK $BUILD_TYPE build (MSYS2) ==="
check_deps

if [ $opt_32_only -eq 0 ]; then
  echo "--- Building x64 ($BUILD_TYPE) ---"
  build_arch 64
fi
if [ $opt_64_only -eq 0 ]; then
  echo "--- Building x32 ($BUILD_TYPE) ---"
  build_arch 32
fi

cleanup

echo "=== Done: $DXVK_OUT_DIR/ ==="
ls -la "$DXVK_OUT_DIR"/x*/
