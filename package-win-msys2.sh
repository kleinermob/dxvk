#!/usr/bin/env bash
#
# Build DXVK for Windows using MSYS2 MinGW toolchains.
# Run from an MSYS2 MINGW64 shell:
#
#   ./package-win-msys2.sh 2.7.1 /c/dxvk-out
#   ./package-win-msys2.sh 2.7.1 /c/dxvk-out --64-only
#   ./package-win-msys2.sh 2.7.1 /c/dxvk-out --dev-build
#

set -e
shopt -s extglob

if [ -z "$1" ] || [ -z "$2" ]; then
  echo "Usage: $0 version destdir [--no-package] [--dev-build] [--64-only] [--32-only]"
  exit 1
fi

DXVK_VERSION="$1"
DXVK_SRC_DIR=$(dirname "$(readlink -f "$0")")
DXVK_BUILD_DIR=$(realpath "$2")"/dxvk-$DXVK_VERSION"
DXVK_ARCHIVE_PATH=$(realpath "$2")"/dxvk-$DXVK_VERSION.tar.gz"

if [ -e "$DXVK_BUILD_DIR" ]; then
  echo "Build directory $DXVK_BUILD_DIR already exists"
  exit 1
fi

shift 2

opt_nopackage=0
opt_devbuild=0
opt_buildid=false
opt_64_only=0
opt_32_only=0

while [ $# -gt 0 ]; do
  case "$1" in
  "--no-package")
    opt_nopackage=1
    ;;
  "--dev-build")
    opt_nopackage=1
    opt_devbuild=1
    ;;
  "--build-id")
    opt_buildid=true
    ;;
  "--64-only")
    opt_64_only=1
    ;;
  "--32-only")
    opt_32_only=1
    ;;
  *)
    echo "Unrecognized option: $1" >&2
    exit 1
  esac
  shift
done

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

  # Resolve to Windows-style path so ninja can find the tools
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

  cd "$DXVK_SRC_DIR"

  # Add both mingw bin dirs to PATH
  export PATH="/mingw64/bin:/mingw32/bin:$PATH"

  local crossfile="$DXVK_BUILD_DIR/.crossfile-win${bits}.txt"
  mkdir -p "$DXVK_BUILD_DIR"
  generate_crossfile "$bits" "$crossfile"

  opt_strip=
  if [ $opt_devbuild -eq 0 ]; then
    opt_strip=--strip
  fi

  meson setup --cross-file "$crossfile"        \
        --buildtype "release"                  \
        --prefix "$DXVK_BUILD_DIR"             \
        $opt_strip                             \
        --bindir "x${bits}"                    \
        --libdir "x${bits}"                    \
        -Db_ndebug=if-release                  \
        -Dbuild_id=$opt_buildid                \
        "$DXVK_BUILD_DIR/build.${bits}"

  cd "$DXVK_BUILD_DIR/build.${bits}"
  ninja install

  if [ $opt_devbuild -eq 0 ]; then
    # Remove non-dll files and build dir
    rm -f "$DXVK_BUILD_DIR/x${bits}/"*.!(dll)
    rm -rf "$DXVK_BUILD_DIR/build.${bits}"
  fi
}

package() {
  cd "$DXVK_BUILD_DIR/.."
  tar -czf "$DXVK_ARCHIVE_PATH" "dxvk-$DXVK_VERSION"
  rm -rf "dxvk-$DXVK_VERSION"
}

# --- Main ---
echo "=== DXVK $DXVK_VERSION Windows build (MSYS2) ==="
check_deps

if [ $opt_32_only -eq 0 ]; then
  echo "--- Building x64 ---"
  build_arch 64
fi
if [ $opt_64_only -eq 0 ]; then
  echo "--- Building x32 ---"
  build_arch 32
fi

if [ $opt_nopackage -eq 0 ]; then
  package
  echo "=== Package: $DXVK_ARCHIVE_PATH ==="
else
  echo "=== Output: $DXVK_BUILD_DIR ==="
fi
