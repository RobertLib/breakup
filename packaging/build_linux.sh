#!/bin/bash
#
# Build the Linux payload: the game, the three libraries it needs, and nothing
# installed.
#
# The development build links whatever SDL3 the machine has through pkg-config,
# which is right for a machine that has one. A player's has not: SDL3 is new
# enough that most distributions in use today ship no package for it at all, and
# SDL3_ttf and SDL3_mixer are newer still — so a build that says "install
# libsdl3-ttf" is a build that does not run. The libraries therefore travel
# inside the payload, exactly as they travel inside the macOS bundle and beside
# the Windows exe, and the binary is told to look beside itself for them.
#
# `$ORIGIN/lib` is what does that, and it is the whole trick: the runtime loader
# expands it to the directory the executable is actually in, so the payload can
# be unpacked anywhere — a home directory, a USB stick, wherever a launcher puts
# it — and still find its own copy. Without it the loader searches the system
# paths, finds either nothing or somebody else's build, and the failure lands as
# a line about a shared object rather than as anything to do with this game.
#
# The three .so files get `$ORIGIN` of their own as well, and that is not
# redundant. Modern linkers emit DT_RUNPATH rather than DT_RPATH, and RUNPATH is
# not transitive: the executable's copy resolves the executable's own
# dependencies and nothing deeper, so libSDL3_ttf.so looking for libSDL3.so is a
# question the exe's rpath does not answer. It happens to work anyway, because
# libSDL3 is earlier in the exe's DT_NEEDED and is already loaded by then — which
# is to say it works because of link order, and link order is not a thing to
# leave a payload's ability to start resting on.
#
# SDL is built from the versions packaging/sdl3_pins.sh holds, read out of that
# file rather than written down again here, for the reason that file gives. The
# release *tag* is the pin here rather than a hash: SDL_ttf needs a freetype
# checkout that no source tarball carries, so this builds from git, and the
# freetype revision comes from SDL_ttf's own .gitmodules — the same place its
# own build gets it.
#
# What is switched off is deliberate, and it is what keeps the payload to three
# files. SDL_ttf gets vendored freetype with zlib, png, bzip2 and brotli disabled
# by its own cmake, and harfbuzz and plutosvg off: harfbuzz is C++ and would put
# libstdc++ into a payload that otherwise needs nothing, and it buys shaping for
# scripts this game does not render — every string in it is Latin. SDL_mixer gets
# every decoder that needs an external library switched off; the one encoded
# format this game ships is Ogg Vorbis, for the nineteen music tracks embedded in the
# binary, and SDL_mixer's own stb_vorbis decodes those without a library. See
# src/lib/audio.c.
set -euo pipefail

# First, because it is the most fundamental thing that can be wrong with running
# this, and every message below reads as advice on a machine where the advice
# applies. The game is linked with the host `cc` against a host-built SDL, so
# there is no cross-build here to be had: on a Mac this would otherwise build a
# macOS binary, tar it as `linux-x86_64` and say `upload this`.
if [ "$(uname -s)" != "Linux" ]; then
    echo "build_linux: builds the Linux payload and needs a Linux;" >&2
    echo "            $(uname -s) cannot produce one. Press the button on" >&2
    echo "            .github/workflows/payloads.yml, which is a Linux for" >&2
    echo "            anybody who has not got one." >&2
    exit 1
fi

root=$(cd "$(dirname "$0")/.." && pwd)
dist=${DIST_DIR:-$root/dist}
sdl_prefix=${SDL_PREFIX:-$root/build/sdl3-linux}
jobs=${JOBS:-$( (nproc 2>/dev/null || echo 4) )}

# shellcheck source=packaging/sdl3_pins.sh
source "$root/packaging/sdl3_pins.sh"

define() { sed -n "s/^#define $1 \"\(.*\)\"/\1/p" "$root/src/version.h"; }
version=$(define BREAKUP_VERSION)
app_name=$(define BREAKUP_APP_NAME)
# See the note in build_windows.sh: the save directory is the header's to name,
# and the README below is the only place a player reads it.
save_org=$(define BREAKUP_SAVE_ORG)
save_app=$(define BREAKUP_SAVE_APP)
[ -n "$version" ] && [ -n "$app_name" ] || {
    echo "build_linux: could not read the version out of src/version.h" >&2
    exit 1
}

# Staged under the name the archive is called, so unpacking it leaves one folder
# with the game's name and version on it rather than a bare `breakup` and a
# `lib` loose in whoever's Downloads directory.
payload=$app_name-$version-linux-x86_64
stage=$dist/stage/$payload

# ---- what a missing X extension costs -------------------------------------
#
# SDL's cmake treats an X extension it cannot find as a hard configure error
# rather than a backend it goes without, and it finds out one extension at a
# time: twenty lines of successful checks and then `Couldn't find dependency
# package for XTEST`, having already spent the clone and most of a minute of
# configuring to get there. The package that is missing is four words; finding
# that out is the whole job.
#
# So the headers are looked for first, and the message names the Debian package
# because that is the distribution the payload is built on
# (.github/workflows/payloads.yml pins the runner). Other distributions spell
# these differently and the header path is the same on all of them, which is why
# the check is on the header.
#
# **This list is advisory and cmake stays the authority**, which is the only
# honest thing to say about a copy of somebody else's build requirements. If SDL
# adds an extension nobody has added here, this passes and cmake fails exactly as
# it would have — the check can be behind, it cannot be wrong. Nothing in this
# repository can hold it, because the fact it would be held against lives in
# SDL's own tree.
preflight_x11()
{
    local missing="" row header package
    for row in \
        "X11/Xlib.h|libx11-dev" \
        "X11/Xutil.h|libx11-dev" \
        "X11/extensions/Xext.h|libxext-dev" \
        "X11/Xcursor/Xcursor.h|libxcursor-dev" \
        "X11/extensions/XInput2.h|libxi-dev" \
        "X11/extensions/Xfixes.h|libxfixes-dev" \
        "X11/extensions/Xrandr.h|libxrandr-dev" \
        "X11/extensions/Xrender.h|libxrender-dev" \
        "X11/extensions/scrnsaver.h|libxss-dev" \
        "X11/extensions/XTest.h|libxtst-dev" \
        "xkbcommon/xkbcommon.h|libxkbcommon-dev"
    do
        header=${row%%|*}
        package=${row##*|}
        if [ ! -e "/usr/include/$header" ] &&
           [ ! -e "/usr/local/include/$header" ]; then
            case " $missing " in
            *" $package "*) ;;
            *) missing="$missing $package" ;;
            esac
        fi
    done
    if [ -n "$missing" ]; then
        echo "build_linux: SDL will not configure without these:$missing" >&2
        echo "            sudo apt-get install -y --no-install-recommends$missing" >&2
        echo "            (or point SDL_PREFIX at an existing SDL3 install)" >&2
        exit 1
    fi
}

# ---- SDL, once per pin ----------------------------------------------------
# Kept in build/ rather than rebuilt every time: it is somebody else's library
# and it is the same bytes until the pin moves. The marker is the library itself,
# so a half-finished build does not read as a finished one.
#
# In this order and not another: SDL_ttf and SDL_mixer both find_package(SDL3),
# so SDL3 has to be installed into the prefix before either of them configures.
# SDL3_LIBS is in link order, which is the same order for the same reason.
preflight_done=0
for lib in "${SDL3_LIBS[@]}"; do
    lib_version=$(sdl3_pin "$lib" 3)
    repo=$(sdl3_pin "$lib" 1)
    [ -f "$sdl_prefix/lib/lib$lib.so" ] && continue

    for tool in cmake git cc tar; do
        command -v "$tool" >/dev/null 2>&1 || {
            echo "build_linux: needs $tool to build $lib $lib_version" >&2
            echo "            (or point SDL_PREFIX at an existing SDL3 install)" >&2
            exit 1
        }
    done
    # Before the clone rather than after it: the point is to fail in the second
    # it takes to stat eleven files, not in the minute it takes to get to the
    # cmake line that would have said the same thing. Once is enough — only SDL
    # itself looks at X.
    if [ "$preflight_done" = 0 ]; then
        preflight_x11
        preflight_done=1
    fi

    src=$root/build/$repo-$lib_version
    if [ ! -d "$src" ]; then
        echo "build_linux: fetching $lib $lib_version"
        rm -rf "$src.part"
        git clone --depth 1 --no-tags --single-branch \
            --branch "release-$lib_version" \
            "https://github.com/libsdl-org/$repo.git" "$src.part"
        mv "$src.part" "$src"
    fi

    # freetype, and only freetype. SDL_ttf's cmake wants it in
    # external/freetype and stops with a FATAL_ERROR if it is not there. Its
    # revision is read out of .gitmodules — the file SDL_ttf's own build reads
    # it from — rather than written down here, where it would be a second
    # opinion about somebody else's dependency. `--filter=blob:none --depth 1`
    # is what external/download.sh uses, for the same reason: the history is
    # not wanted, the tree is.
    #
    # Outside the clone above, deliberately: this checkout is shared with
    # build_web.sh, and either script may have made it. Nested inside, a tree
    # cloned by the one that does not need freetype is a tree the other cannot
    # build out of and will not fix, because the directory it tests for is
    # already there.
    if [ "$lib" = "SDL3_ttf" ] && [ ! -f "$src/external/freetype/CMakeLists.txt" ]; then
        ft_url=$(git -C "$src" config -f .gitmodules \
            --get submodule.external/freetype.url)
        ft_branch=$(git -C "$src" config -f .gitmodules \
            --get submodule.external/freetype.branch)
        [ -n "$ft_url" ] && [ -n "$ft_branch" ] || {
            echo "build_linux: no freetype submodule in $lib's .gitmodules" >&2
            exit 1
        }
        echo "build_linux: fetching freetype ($ft_branch) for $lib"
        rm -rf "$src/external/freetype"
        git clone --filter=blob:none --depth 1 --branch "$ft_branch" \
            "$ft_url" "$src/external/freetype"
    fi

    args=(
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX="$sdl_prefix"
        -DCMAKE_PREFIX_PATH="$sdl_prefix"
        -DBUILD_SHARED_LIBS=ON
    )
    case $lib in
    SDL3)
        # Video and audio backends left to cmake's own detection: this payload
        # is for somebody who has a screen and speakers.
        args+=(-DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF)
        ;;
    SDL3_ttf)
        # `$ORIGIN` so this library finds libSDL3.so beside itself rather than
        # relying on the executable having loaded it first. Single-quoted here
        # and untouched by cmake, which expands `${}` and not a bare `$`.
        args+=(
            -DCMAKE_INSTALL_RPATH='$ORIGIN'
            -DSDLTTF_VENDORED=ON
            -DSDLTTF_HARFBUZZ=OFF
            -DSDLTTF_PLUTOSVG=OFF
            -DSDLTTF_SAMPLES=OFF
            -DSDLTTF_INSTALL_CPACK=OFF
        )
        ;;
    SDL3_mixer)
        # Every decoder that would pull in a library of its own, off. What is
        # left — WAVE, AIFF, VOC, AU and the single-header dr_flac, dr_mp3 and
        # stb_vorbis — is source that ships inside SDL_mixer and costs nothing.
        #
        # One of those is load-bearing: the soundtrack is nineteen Ogg Vorbis files
        # embedded in the binary, and stb_vorbis is what decodes them once
        # VORBISFILE is off. It is on by default, and it is asked for by name
        # anyway — a payload that cannot play its own music because an upstream
        # default moved is a silent game, and silence does not fail a build.
        args+=(
            -DCMAKE_INSTALL_RPATH='$ORIGIN'
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

    echo "build_linux: building $lib $lib_version into $sdl_prefix"
    build=$root/build/$lib-$lib_version-build
    cmake -S "$src" -B "$build" "${args[@]}"
    cmake --build "$build" --parallel "$jobs"
    cmake --install "$build"

    [ -f "$sdl_prefix/lib/lib$lib.so" ] || {
        echo "build_linux: $lib installed no lib$lib.so into $sdl_prefix" >&2
        exit 1
    }
done

# ---- the game -------------------------------------------------------------
# `'$$ORIGIN/lib'` gets through **two** expansions, and both of them have to be
# accounted for or the payload only runs on the machine that built it.
#
# make eats one `$`, so `$$ORIGIN` reaches the recipe as `$ORIGIN` — and the
# recipe is a *shell* command, where an unquoted `$ORIGIN` is an unset variable
# that expands to nothing. That leaves `-Wl,-rpath,/lib`: a binary that looks for
# its SDL in the system's own `/lib`, finds either nothing or somebody else's,
# and fails with a line about a shared object rather than anything to do with
# this game. The single quotes are what stop the second expansion; they are
# consumed by that shell and the linker gets the literal string it needs.
#
# `make -n` with these overrides prints the link line, which is the way to check
# this rather than reason about it: the wrong form prints `/lib` in it plainly.
#
# `embed` first and on its own. The makefile orders this itself — every object
# has an order-only prerequisite on the header, and depfiles decide what
# recompiles — so this line is belt and braces: a failure to generate the
# header is reported as itself rather than as a compile error further down.
# The target is named explicitly because `all` is `debug` in this makefile and
# would produce breakup_dbg built -O0 with DEBUG defined.
exe=$root/build/linux/breakup
cflags_sdl="-I$sdl_prefix/include"
ldflags_sdl="-L$sdl_prefix/lib"
for lib in "${SDL3_LIBS[@]}"; do
    ldflags_sdl="$ldflags_sdl -l$lib"
done

make -C "$root" embed
make -C "$root" -j"$jobs" \
    BUILD_DIR=build/linux TARGET=build/linux/breakup \
    CFLAGS="-std=c17 -Wall -Wextra -Wpedantic -O2 -DNDEBUG $cflags_sdl" \
    LDFLAGS="$ldflags_sdl -lm -Wl,-rpath,'\$\$ORIGIN/lib'" \
    build/linux/breakup

[ -x "$exe" ] || { echo "build_linux: no binary at $exe" >&2; exit 1; }

# ---- the payload ----------------------------------------------------------
rm -rf "$stage"
mkdir -p "$stage/lib"
install -m 755 "$exe" "$stage/breakup"
# Stripped for the same reason the .app's binary is: debug symbols are most of
# the download and none of the game. `|| true` because a toolchain without
# `strip` is a smaller payload's worth of nothing to stop a release for.
strip "$stage/breakup" 2>/dev/null || true

# The versioned file and the SONAME symlink beside it, because the binary asks
# for `libSDL3.so.0` and a payload holding only `libSDL3.so.0.4.14` answers a
# question nobody asked. `cp -a` keeps the symlink a symlink.
for lib in "${SDL3_LIBS[@]}"; do
    for so in "$sdl_prefix"/lib/"lib$lib".so*; do
        cp -a "$so" "$stage/lib/"
    done
done

cat >"$stage/README.txt" <<TXT
$app_name $version — Linux x86_64

Run ./breakup

lib/ holds the SDL3, SDL3_ttf and SDL3_mixer this build was made against; the
game looks for them there and needs nothing installed. Keep the two together.

Settings, progress and high scores are kept in \$XDG_DATA_HOME/$save_org/$save_app
(usually ~/.local/share/$save_org/$save_app) and nowhere else. Deleting that
directory starts the game over.
TXT

archive=$dist/$payload.tar.gz
rm -f "$archive"
tar -C "$dist/stage" -czf "$archive" "$payload"
# The staging tree is an intermediate, not an artifact. Left standing it makes
# dist/ hold the payload twice — the archive and a loose copy of everything in it
# — which is how somebody comes to upload the folder instead of the tarball.
rm -rf "$dist/stage"

echo
echo "$archive"
echo "  $(du -h "$archive" | cut -f1) — upload this."
echo "  glibc floor is this machine's: $(getconf GNU_LIBC_VERSION 2>/dev/null || echo unknown)"
