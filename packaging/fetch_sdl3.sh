#!/bin/bash
#
# Put the official universal SDL3 frameworks in vendor/, for the shipped app to
# be built and to ship against.
#
# The development build links Homebrew's SDL3, which is right for a machine that
# has Homebrew on it and wrong for everybody else: it is arm64 only and it is
# built for the macOS it was poured on, so a bundle wrapped around it runs on
# this Mac and refuses to start on any other. libsdl.org's own releases carry
# both slices and a macOS 11 floor, which is the difference between a build and
# a build somebody else can run.
#
# The versions and their hashes are pinned in packaging/sdl3_pins.sh, and read
# from there rather than written down here: a shipped binary must be traceable to
# the exact libraries it was linked against, "whatever the latest release was on
# the day" is not that, and three platform scripts each holding their own copy of
# a version number is three version numbers.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
vendor=${1:-$root/vendor}
cache=$root/build

# shellcheck source=packaging/sdl3_pins.sh
source "$root/packaging/sdl3_pins.sh"

mkdir -p "$vendor" "$cache"

for release in "${SDL3_PINS[@]}"; do
    IFS='|' read -r repo name version sha _ <<< "$release"
    framework="$vendor/$name.framework"

    if [ -d "$framework" ]; then
        echo "vendor: $name.framework already present"
        continue
    fi

    dmg="$cache/$name-$version.dmg"
    if [ ! -f "$dmg" ]; then
        echo "vendor: downloading $name $version"
        curl --fail --location --progress-bar --output "$dmg.part" \
            "https://github.com/libsdl-org/$repo/releases/download/release-$version/$name-$version.dmg"
        mv "$dmg.part" "$dmg"
    fi

    have=$(shasum -a 256 "$dmg" | awk '{print $1}')
    if [ "$have" != "$sha" ]; then
        echo "vendor: sha256 mismatch for $dmg" >&2
        echo "  expected $sha" >&2
        echo "  got      $have" >&2
        exit 1
    fi

    mount=$(mktemp -d "${TMPDIR:-/tmp}/sdl3-mount.XXXXXX")
    trap 'hdiutil detach -quiet "$mount" >/dev/null 2>&1 || true; rmdir "$mount" 2>/dev/null || true' EXIT
    hdiutil attach -nobrowse -quiet "$dmg" -mountpoint "$mount"

    src="$mount/$name.xcframework/macos-arm64_x86_64/$name.framework"
    if [ ! -d "$src" ]; then
        echo "vendor: no macOS slice in $name.xcframework" >&2
        exit 1
    fi

    # ditto rather than cp: a framework is symlinks (Versions/Current, Headers,
    # the binary), and a copy that flattens them is not a framework any more.
    ditto "$src" "$framework"

    hdiutil detach -quiet "$mount"
    rmdir "$mount"
    trap - EXIT

    echo "vendor: $framework ($(lipo -archs "$framework/$name"), minos $(otool -l "$framework/$name" | awk '/minos/{print $2; exit}'))"
done
