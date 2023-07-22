#!/bin/bash
#
# Build the browser payload: dist/Breakup-<v>-web.zip, an itch.io HTML5 upload.
#
# This is the fourth platform and the first one that is not a download. itch.io
# serves an HTML5 build from an iframe on the game's own page, which is how most
# of that site is actually played — a visitor who has to download a binary,
# unblock it and find it again in a Downloads folder is a visitor who has
# already left.
#
# It is the same four steps as build_linux.sh, build_windows.sh and
# build_macos.sh — the libraries, the game, the payload, the archive — and the
# same rule: this script goes from the tree to one archive in dist/ and stops.
# Nothing here uploads anything.
#
# Three things about it are different, and each is somebody else's rule rather
# than a taste of ours:
#
#   **The archive has no folder in it.** Linux and Windows deliberately unpack
#   into one named directory, because the alternative is an exe and three DLLs
#   loose in somebody's Downloads. itch.io's HTML5 hosting is the exact
#   opposite: it looks for `index.html` at the *root* of the zip and offers no
#   way to say otherwise, so a payload nested the way its three siblings are
#   nested is a page that will not start and says nothing about why.
#
#   **The libraries are built from source, and one cmake flag is the reason.**
#   There is no published wasm build of SDL and there could not usefully be one:
#   whether a player's settings, unlocked levels and high scores survive closing
#   the tab is `SDL_EMSCRIPTEN_PERSISTENT_PATH`, a *compile-time* option of SDL's
#   own. See the note over the cmake line below. The versions are the pins in
#   packaging/sdl3_pins.sh, read from there rather than written down again, and
#   the release tag is the pin here for the same reason build_linux.sh builds
#   from git: SDL_ttf needs a freetype checkout that no source tarball carries.
#
#   **There are no threads.** Threads on the web need COOP/COEP headers on the
#   server, itch.io does not set them, and a build that needs them does not start
#   there at all — not a slow path, a blank page. Nothing in src/ starts a
#   thread, so this costs nothing.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
dist=${DIST_DIR:-$root/dist}
sdl_prefix=${SDL_PREFIX:-$root/build/sdl3-web}
jobs=${JOBS:-$( (sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4) )}

# ---- the toolchain --------------------------------------------------------
#
# emcc is not on a default PATH even once emsdk is installed: its own installer
# tells you to source a script, and forgetting to is by far the most likely way
# to arrive here. So this sources it where it can and the failure names what to
# run rather than saying "command not found".
#
# EMSDK is honoured when it is set because that is what emsdk_env.sh exports,
# which makes `EMSDK=~/emsdk make web` work from a shell that has never sourced
# anything.
if ! command -v emcc >/dev/null 2>&1; then
    if [ -n "${EMSDK:-}" ] && [ -f "$EMSDK/emsdk_env.sh" ]; then
        # shellcheck disable=SC1091
        source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1 || true
    elif [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
        # shellcheck disable=SC1091
        source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1 || true
    fi
fi
if ! command -v emcc >/dev/null 2>&1; then
    cat >&2 <<'MSG'
build_web: no emcc on PATH.

    git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
    cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest
    source ~/emsdk/emsdk_env.sh

emsdk needs a python 3.10 or newer to install itself; on a Mac whose python3 is
Xcode's, `EMSDK_PYTHON=$(brew --prefix)/bin/python3` is the fix.
MSG
    exit 1
fi

# SDL's own docs/README-emscripten.md is blunt about this: it is possible to
# build against emscripten 3.x and "several things will be silently broken".
# Silently is the word that earns this check — the failure is not a compile
# error, it is a game that behaves oddly in a browser somebody else is using.
EMCC_MIN_MAJOR=4
emcc_version=$(emcc --version | sed -n '1s/.*clang-like replacement + linker emulating GNU ld) \([0-9][0-9.]*\).*/\1/p')
if [ -z "$emcc_version" ]; then
    emcc_version=$(emcc -dumpversion 2>/dev/null || echo 0)
fi
if [ "${emcc_version%%.*}" -lt "$EMCC_MIN_MAJOR" ] 2>/dev/null; then
    echo "build_web: emcc $emcc_version is older than $EMCC_MIN_MAJOR.0;" \
         "SDL documents 3.x as silently broken. Run: emsdk install latest" >&2
    exit 1
fi

for tool in cmake git zip xxd pkg-config; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build_web: needs $tool" >&2
        echo "           macOS: brew install $tool" >&2
        exit 1
    }
done

# shellcheck source=packaging/sdl3_pins.sh
source "$root/packaging/sdl3_pins.sh"

define() { sed -n "s/^#define $1 \"\(.*\)\"/\1/p" "$root/src/version.h"; }
version=$(define BREAKUP_VERSION)
app_name=$(define BREAKUP_APP_NAME)
[ -n "$version" ] && [ -n "$app_name" ] || {
    echo "build_web: could not read the version out of src/version.h" >&2
    exit 1
}

# The frame the game draws, read off the header rather than written here: a
# resolution spelled in a shell script is a second copy of a #define, and the
# page's canvas is sized from it.
#
# `tail -1` and not `head -1`, and it is load-bearing: globals.h defines
# SCREEN_HEIGHT twice, the iPhone's 1350 first and the 600 every other platform
# draws at in the `#else`. The first match is the wrong one.
screen() {
    sed -n "s/^#define $1 \([0-9][0-9]*\).*/\1/p" "$root/src/globals.h" | tail -1
}
view_w=$(screen SCREEN_WIDTH)
view_h=$(screen SCREEN_HEIGHT)
if [ -z "$view_w" ] || [ -z "$view_h" ]; then
    echo "build_web: could not read SCREEN_WIDTH/SCREEN_HEIGHT from src/globals.h" >&2
    exit 1
fi

payload=$app_name-$version-web
stage=$dist/stage/$payload

# ---- SDL, once per pin ----------------------------------------------------
#
# Kept in build/ rather than rebuilt every time: it is somebody else's library
# and it is the same bytes until the pin moves. The marker is the installed
# archive, so a half-finished build does not read as a finished one.
#
# In this order and not another: SDL_ttf and SDL_mixer both find_package(SDL3),
# so SDL3 has to be installed into the prefix before either of them configures.
# SDL3_LIBS is in link order, which is the same order for the same reason.
for lib in "${SDL3_LIBS[@]}"; do
    lib_version=$(sdl3_pin "$lib" 3)
    repo=$(sdl3_pin "$lib" 1)
    [ -f "$sdl_prefix/lib/lib$lib.a" ] && continue

    # Shared with build_linux.sh, deliberately: same tag, same tree, same
    # directory name, and whichever script gets there first saves the other the
    # clone. Nothing in a checkout is platform-specific — only the build
    # directories and the prefix below are.
    src=$root/build/$repo-$lib_version
    if [ ! -d "$src" ]; then
        echo "build_web: fetching $lib $lib_version"
        rm -rf "$src.part"
        git clone --depth 1 --no-tags --single-branch \
            --branch "release-$lib_version" \
            "https://github.com/libsdl-org/$repo.git" "$src.part"
        mv "$src.part" "$src"
    fi

    # freetype, and only freetype. SDL_ttf's cmake wants it in
    # external/freetype and stops with "No freetype sources found" if it is not
    # there. Its revision is read out of .gitmodules — the file SDL_ttf's own
    # build reads it from — rather than written down here, where it would be a
    # second opinion about somebody else's dependency. `--filter=blob:none
    # --depth 1` is what SDL_ttf's own external/download.sh uses, and for the
    # same reason: the history is not wanted, the tree is.
    #
    # Outside the clone above, deliberately: this checkout is shared with
    # build_linux.sh, and either script may have made it. Nested inside, a tree
    # cloned by the one that does not need freetype is a tree the other cannot
    # build out of and will not fix, because the directory it tests for is
    # already there.
    if [ "$lib" = "SDL3_ttf" ] && [ ! -f "$src/external/freetype/CMakeLists.txt" ]; then
        ft_url=$(git -C "$src" config -f .gitmodules \
            --get submodule.external/freetype.url)
        ft_branch=$(git -C "$src" config -f .gitmodules \
            --get submodule.external/freetype.branch)
        [ -n "$ft_url" ] && [ -n "$ft_branch" ] || {
            echo "build_web: no freetype submodule in $lib's .gitmodules" >&2
            exit 1
        }
        echo "build_web: fetching freetype ($ft_branch) for $lib"
        rm -rf "$src/external/freetype"
        git clone --filter=blob:none --depth 1 --branch "$ft_branch" \
            "$ft_url" "$src/external/freetype"
    fi

    # Static, all three of them, and that is not a size decision: a browser has
    # no loader to hand a .so to. Everything the page runs is in one .wasm.
    #
    # CMAKE_FIND_ROOT_PATH is the one line here that build_linux.sh does not
    # need, and leaving it out is a confusing failure rather than a quiet one:
    # emscripten's toolchain file sets the find-root mode for packages to ONLY,
    # so find_package() searches emscripten's own sysroot and *nothing else* —
    # CMAKE_PREFIX_PATH included. SDL_ttf then stops with "could not find a
    # package configuration file provided by SDL3" while libSDL3.a is sitting
    # installed in the prefix named on the same command line.
    args=(
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX="$sdl_prefix"
        -DCMAKE_PREFIX_PATH="$sdl_prefix"
        -DCMAKE_FIND_ROOT_PATH="$sdl_prefix"
        -DBUILD_SHARED_LIBS=OFF
    )
    case $lib in
    SDL3)
        #
        # SDL_EMSCRIPTEN_PERSISTENT_PATH is the whole reason this is a source
        # build.
        #
        # A browser tab's filesystem is RAM: write a file, close the tab, the
        # file is gone. This game keeps one — save.txt under SDL_GetPrefPath,
        # holding the volumes, the fullscreen flag, how many levels are unlocked
        # and the five high scores — and without this option every player would
        # lose all of it every time they closed the page, silently, because
        # saving *works*: it is only the disk that stops existing.
        #
        # Given the option, SDL mounts the browser's IndexedDB here with
        # `autoPersist: true`, waits for it to sync before it calls SDL_main,
        # and hands SDL_GetPrefPath a directory underneath it. So src/lib/save.c
        # is untouched and what it writes survives — but only for a program that
        # goes through SDL's own entry point, which is what the SDL_main.h
        # include at the top of src/main.c is for.
        #
        args+=(
            -DSDL_EMSCRIPTEN_PERSISTENT_PATH=/storage
            -DSDL_SHARED=OFF -DSDL_STATIC=ON
            -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
        )
        ;;
    SDL3_ttf)
        # Vendored freetype with everything optional off, and harfbuzz off with
        # it: harfbuzz is C++ and would put a C++ runtime into a payload that
        # otherwise needs none, and it buys shaping for scripts this game does
        # not render — every string in it is Latin.
        args+=(
            -DSDLTTF_VENDORED=ON
            -DSDLTTF_HARFBUZZ=OFF
            -DSDLTTF_PLUTOSVG=OFF
            -DSDLTTF_SAMPLES=OFF
            -DSDLTTF_INSTALL_CPACK=OFF
        )
        ;;
    SDL3_mixer)
        # Every decoder that would need a library of its own, off — the same
        # list build_linux.sh switches off, for the same reason. What is left is
        # source that ships inside SDL_mixer.
        #
        # One of those is load-bearing: the soundtrack is nineteen Ogg Vorbis
        # files embedded in the binary, and stb_vorbis is what decodes them once
        # VORBISFILE is off. It is on by default and it is asked for by name
        # anyway — a payload that cannot play its own music because an upstream
        # default moved is a silent game, and silence does not fail a build.
        args+=(
            -DSDLMIXER_VENDORED=OFF
            -DSDLMIXER_GME=OFF
            -DSDLMIXER_MOD=OFF
            -DSDLMIXER_MP3_MPG123=OFF
            -DSDLMIXER_OPUS=OFF
            -DSDLMIXER_VORBIS_STB=ON
            -DSDLMIXER_VORBIS_VORBISFILE=OFF
            -DSDLMIXER_FLAC_LIBFLAC=OFF
            -DSDLMIXER_WAVPACK=OFF
            -DSDLMIXER_MIDI=OFF
            -DSDLMIXER_TESTS=OFF
            -DSDLMIXER_EXAMPLES=OFF
            -DSDLMIXER_INSTALL_CPACK=OFF
        )
        ;;
    esac

    echo "build_web: compiling $lib $lib_version for wasm"
    build=$root/build/$lib-$lib_version-web-build
    emcmake cmake -S "$src" -B "$build" "${args[@]}" >/dev/null
    cmake --build "$build" --parallel "$jobs" >/dev/null
    cmake --install "$build" >/dev/null

    [ -f "$sdl_prefix/lib/lib$lib.a" ] || {
        echo "build_web: $lib installed no lib$lib.a into $sdl_prefix" >&2
        exit 1
    }
done

# The persistent path is a compile-time decision and this is the one place it
# leaves a trace: SDL's own pkg-config puts -lidbfs.js in Libs when it has been
# built with one. Checking it here rather than trusting the cmake line is what
# stops a stale build/sdl3-web from an earlier invocation being linked in
# silently — which would be a game that saves nothing and says so nowhere.
if ! grep -q -- "-lidbfs.js" "$sdl_prefix/lib/pkgconfig/sdl3.pc"; then
    echo "build_web: the SDL in $sdl_prefix was built without a persistent" >&2
    echo "           path, so nobody's settings, progress or high scores would" >&2
    echo "           survive closing the tab. Remove that directory and run" >&2
    echo "           this again." >&2
    exit 1
fi

# ---- the game -------------------------------------------------------------
#
# The same makefile the other three use, with emcc as the compiler and a .js
# where the executable goes — emcc reads the output extension and writes
# breakup.js beside breakup.wasm.
#
# The link line comes out of the prefix's own pkg-config rather than being
# spelled here, and `--static` is what makes that the right question to ask: a
# static libSDL3_ttf.a does not carry the freetype it was built against, and the
# .pc files are where the order and the extra archives are already written down
# correctly. -lidbfs.js arrives the same way, which is what the check above is
# reading.
export PKG_CONFIG_LIBDIR=$sdl_prefix/lib/pkgconfig
cflags_sdl=$(pkg-config --cflags sdl3 sdl3-ttf sdl3-mixer)
ldflags_sdl=$(pkg-config --libs --static sdl3 sdl3-ttf sdl3-mixer)

# The link flags that are ours rather than SDL's, and why each is there:
#
#   ALLOW_MEMORY_GROWTH  SDL's README asks for it for anything of any size, and
#   MAXIMUM_MEMORY=1gb   this one starts with 18 MB of Ogg Vorbis in its data.
#   INITIAL_MEMORY=96mb  And that is what this is for. Emscripten starts the
#                        heap at 16 MB, which is less than the game's *static*
#                        data - the embedded font, levels and music come to
#                        about 18 MB before a single allocation - so the link
#                        fails outright with "initial memory too small". It has
#                        to be the constant rather than something computed,
#                        since the linker is the one doing the arithmetic; the
#                        headroom over 18 is the heap this starts with, and
#                        growth covers whatever the game asks for after that.
#
#                        It was 45 MB of static data before the soundtrack was
#                        re-encoded (see NOTICE.md), so 96 is now more headroom
#                        than it was picked for rather than less. It is left
#                        where it is on purpose: ALLOW_MEMORY_GROWTH makes an
#                        over-estimate cost a larger first allocation and
#                        nothing else, while an under-estimate is a link error
#                        found by whoever next runs `make web`.
#   STACK_SIZE=4mb       Emscripten's default stack is 64 KB, which is far less
#                        than any of the three desktop platforms hands a thread
#                        — and a wasm stack that runs off the end corrupts what
#                        is under it rather than faulting. The game's own frames
#                        are small; SDL_ttf's rasterizer and SDL_mixer's
#                        decoders are not this program's to reason about.
#   INVOKE_RUN=0         Hold main back until the page's PLAY button is pressed,
#                        so that the first frame drawn is also the first frame
#                        allowed to make a sound. A browser throws audio away
#                        until the page has been clicked on, and SDL will open a
#                        device and feed a muted one for as long as that lasts —
#                        which is a menu whose music never arrives.
#   EXPORTED_RUNTIME_METHODS=callMain
#                        The one runtime method the page needs, and the thing
#                        INVOKE_RUN=0 is useless without.
#
# `embed` first and on its own, as in the other two scripts: it is cheap, it is
# 260 MB of generated header, and having it already in place makes the -j build
# below one thing rather than two.
make -C "$root" embed
make -C "$root" -j"$jobs" \
    CC=emcc \
    BUILD_DIR=build/web TARGET=build/web/breakup.js \
    CFLAGS="-std=c17 -Wall -Wextra -Wpedantic -O2 -DNDEBUG $cflags_sdl" \
    LDFLAGS="$ldflags_sdl -lm \
             -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=1gb \
             -sINITIAL_MEMORY=96mb -sSTACK_SIZE=4mb \
             -sINVOKE_RUN=0 -sEXPORTED_RUNTIME_METHODS=callMain" \
    build/web/breakup.js

for f in breakup.js breakup.wasm; do
    [ -f "$root/build/web/$f" ] || {
        echo "build_web: emcc produced no $f" >&2; exit 1; }
done

# ---- the payload ----------------------------------------------------------
#
# index.html at the root, and nothing above it. See the note at the top.
rm -rf "$stage"
mkdir -p "$stage"
cp "$root/build/web/breakup.js" "$stage/breakup.js"
cp "$root/build/web/breakup.wasm" "$stage/breakup.wasm"

sed -e "s|@BREAKUP_TITLE@|$app_name|g" \
    -e "s|@BREAKUP_NAME_UPPER@|$(echo "$app_name" | tr '[:lower:]' '[:upper:]')|g" \
    -e "s|@SCREEN_WIDTH@|$view_w|g" \
    -e "s|@SCREEN_HEIGHT@|$view_h|g" \
    -e "s|@BREAKUP_JS@|breakup.js|g" \
    -e "s|@BREAKUP_WASM@|breakup.wasm|g" \
    "$root/packaging/web_shell.html" >"$stage/index.html"

# A placeholder that survived is a page with `@SCREEN_WIDTH@` in its Javascript,
# which is a blank canvas and a console error nobody is going to read.
if grep -q "@[A-Z_]\+@" "$stage/index.html"; then
    echo "build_web: unfilled placeholder in index.html:" >&2
    grep -o "@[A-Z_]\+@" "$stage/index.html" | sort -u >&2
    exit 1
fi

# No README.txt and no .itch.toml. Both of those answer "which file do I run",
# and a page has no such question in it: the shop already knows the answer is
# index.html, and there is nothing beside it for a player to open by mistake.

archive=$dist/$payload.zip
mkdir -p "$dist"
rm -f "$archive"
# Zipped from inside the staging directory rather than above it, which is the
# whole difference from its three siblings: what has to be at the top of this
# archive is index.html and not a folder containing it.
(cd "$stage" && zip -q -r "$archive" .)
# The staging tree is an intermediate, not an artifact. Left standing it makes
# dist/ hold the payload twice — the archive and a loose copy of everything in
# it — which is how somebody comes to upload the folder instead of the zip.
rm -rf "$dist/stage"

echo
echo "$archive"
echo "  $(du -h "$archive" | cut -f1) — upload this, tick \"This file will be"
echo "  played in the browser\", and set the viewport to ${view_w}x${view_h}."
