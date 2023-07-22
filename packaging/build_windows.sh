#!/bin/bash
#
# Cross-build the Windows payload from a Unix machine: breakup.exe, the three
# SDL DLLs beside it, and nothing to install.
#
# mingw-w64 rather than MSVC, for one reason: this tree is C17 built with
# `-Wall -Wextra -Wpedantic` and it links nothing but libc, libm and SDL — there
# is no Windows in it anywhere. Every include in src/ is either SDL or the C
# standard library, `TargetConditionals.h` is already behind `#ifdef __APPLE__`,
# and `UNUSED` already has a `_WIN32` arm. So what a Windows build needs is a
# compiler that targets Windows, and a gcc that does is a far shorter path than a
# second project file that would then have to be kept in step with the makefile.
#
# SDL comes from libsdl.org's own mingw development releases, at the versions and
# hashes packaging/sdl3_pins.sh holds and read out of that file rather than
# written down again: a shipped binary has to be traceable to the libraries it
# was linked against. Each archive carries an import library and a DLL, and the
# DLLs travel in the payload beside the exe — where Windows looks first — so the
# player installs nothing.
#
# There are exactly three DLLs and there is no fourth. freetype and harfbuzz are
# linked inside SDL3_ttf.dll by libsdl.org's build, and so are SDL3_mixer's
# decoders — including the Ogg Vorbis one the nineteen embedded music tracks are
# decoded by. Nothing else has to travel beside the exe for either. `-static-libgcc` is what keeps the compiler's own
# runtime out of the folder too: the alternative is shipping libgcc_s_seh-1.dll
# for the sake of something nobody chose, and a missing one of those is a dialog
# box about a DLL rather than anything about this game. `-mwindows` makes the
# process a GUI application, so no console window opens behind the game — on
# mingw-w64 that is a subsystem flag only, and `int main(void)` stays the entry
# point, which is why src/main.c needs no WinMain.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
dist=${DIST_DIR:-$root/dist}
triple=${MINGW_TRIPLE:-x86_64-w64-mingw32}
cc=${MINGW_CC:-$triple-gcc}
jobs=${JOBS:-$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) )}

if ! command -v "$cc" >/dev/null 2>&1; then
    echo "build_windows: no $cc on PATH" >&2
    echo "               macOS: brew install mingw-w64" >&2
    echo "               Debian/Ubuntu: sudo apt-get install mingw-w64" >&2
    exit 1
fi

for tool in zip curl; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build_windows: needs $tool" >&2; exit 1; }
done

# shellcheck source=packaging/sdl3_pins.sh
source "$root/packaging/sdl3_pins.sh"

define() { sed -n "s/^#define $1 \"\(.*\)\"/\1/p" "$root/src/version.h"; }
version=$(define BREAKUP_VERSION)
app_name=$(define BREAKUP_APP_NAME)
# The save directory is `SDL_GetPrefPath(BREAKUP_SAVE_ORG, BREAKUP_SAVE_APP)`,
# and the README below is the only place a player is ever told where it is — so
# it is read out of the header rather than typed here. Typed here, the one
# sentence somebody needs in order to delete their save becomes a third copy of
# a name only version.h owns, and a wrong path is worse than none.
save_org=$(define BREAKUP_SAVE_ORG)
save_app=$(define BREAKUP_SAVE_APP)
[ -n "$version" ] && [ -n "$app_name" ] || {
    echo "build_windows: could not read the version out of src/version.h" >&2
    exit 1
}

# Staged under the name the archive is called, so unpacking it leaves one named
# folder rather than an exe and three DLLs loose in a Downloads directory —
# which on Windows is the difference between the game finding SDL3.dll and not.
payload=$app_name-$version-windows-x64
stage=$dist/stage/$payload

sha256_of() {
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        sha256sum "$1" | awk '{print $1}'
    fi
}

# ---- SDL, once per pin ----------------------------------------------------
# Kept in build/ rather than refetched every time: it is somebody else's library
# and it is the same bytes until the pin moves. The marker is the import library,
# so a half-extracted archive does not read as a finished one.
cflags_sdl=""
ldflags_sdl=""
dlls=()
for lib in "${SDL3_LIBS[@]}"; do
    lib_version=$(sdl3_pin "$lib" 3)
    lib_sha=$(sdl3_pin "$lib" 5)
    repo_dir=$root/build/$lib-devel-$lib_version-mingw
    prefix=$repo_dir/$lib-$lib_version/$triple

    if [ ! -f "$prefix/lib/lib$lib.dll.a" ]; then
        archive=$root/build/$lib-devel-$lib_version-mingw.tar.gz
        repo=$(sdl3_pin "$lib" 1)
        url=https://github.com/libsdl-org/$repo/releases/download/release-$lib_version/$lib-devel-$lib_version-mingw.tar.gz
        mkdir -p "$root/build"
        if [ ! -f "$archive" ]; then
            echo "build_windows: downloading $lib $lib_version (mingw)"
            curl --fail --location --progress-bar --output "$archive.part" "$url"
            mv "$archive.part" "$archive"
        fi

        have=$(sha256_of "$archive")
        if [ "$have" != "$lib_sha" ]; then
            echo "build_windows: sha256 mismatch for $archive" >&2
            echo "  expected $lib_sha" >&2
            echo "  got      $have" >&2
            exit 1
        fi

        rm -rf "$repo_dir"
        mkdir -p "$repo_dir"
        tar -C "$repo_dir" -xzf "$archive"
        if [ ! -f "$prefix/lib/lib$lib.dll.a" ]; then
            echo "build_windows: no $triple slice in $archive" >&2
            exit 1
        fi
    fi

    cflags_sdl="$cflags_sdl -I$prefix/include"
    ldflags_sdl="$ldflags_sdl -L$prefix/lib -l$lib"
    dlls+=("$prefix/bin/$lib.dll")
done

# ---- the game -------------------------------------------------------------
# `embed` first and on its own: the makefile carries no depfiles, so under -j it
# would race the compiles that include the header it generates.
#
# The target is named explicitly rather than reached through `all`, because `all`
# is `debug` in this makefile and would produce breakup_dbg built -O0 with
# DEBUG defined. CFLAGS and LDFLAGS are given whole on the command line, where
# they beat the target-specific assignments in the makefile as well as the
# pkg-config that has no idea about a Windows SDL3.
exe=$root/build/windows/breakup.exe
make -C "$root" embed
make -C "$root" -j"$jobs" \
    CC="$cc" \
    BUILD_DIR=build/windows TARGET=build/windows/breakup.exe \
    CFLAGS="-std=c17 -Wall -Wextra -Wpedantic -O2 -DNDEBUG$cflags_sdl" \
    LDFLAGS="$ldflags_sdl -lm -mwindows -static-libgcc" \
    build/windows/breakup.exe

[ -f "$exe" ] || { echo "build_windows: no binary at $exe" >&2; exit 1; }

# ---- the payload ----------------------------------------------------------
rm -rf "$stage"
mkdir -p "$stage"
cp "$exe" "$stage/breakup.exe"
# Stripped for the same reason the .app's binary is: debug symbols are most of
# the download and none of the game. `|| true` because a toolchain without a
# strip for this triple is a smaller download's worth of nothing to stop a
# release for.
"$triple-strip" "$stage/breakup.exe" 2>/dev/null || true

for dll in "${dlls[@]}"; do
    cp "$dll" "$stage/$(basename "$dll")"
done

cat >"$stage/README.txt" <<TXT
$app_name $version — Windows x64

Run breakup.exe

The three SDL3 DLLs beside it are the libraries this build was made against.
Keep them next to the exe; nothing needs installing.

Settings, progress and high scores are kept in %APPDATA%\\$save_org\\$save_app and
nowhere else. Deleting that folder starts the game over.

Windows may warn that the publisher is unknown: this build is not signed with
an Authenticode certificate. The source is at
https://github.com/RobertLib/breakup if you would rather build it yourself.
TXT

archive=$dist/$payload.zip
rm -f "$archive"
(cd "$dist/stage" && zip -q -r "$archive" "$payload")
# The staging tree is an intermediate, not an artifact. Left standing it makes
# dist/ hold the payload twice — the archive and a loose copy of everything in it
# — which is how somebody comes to upload the folder instead of the zip.
rm -rf "$dist/stage"

echo
echo "$archive"
echo "  $(du -h "$archive" | cut -f1) — unsigned, upload this."
