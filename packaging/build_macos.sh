#!/bin/bash
#
# The macOS release, end to end: compile both slices against the vendored
# universal SDL3 frameworks, wrap them in dist/Breakup.app with those
# frameworks inside it, sign it with Developer ID, get it a notarization ticket
# from Apple, staple that in, and cut the two things a player can be handed —
# a zip and a DMG.
#
# One script, one output, and the order is not yours to choose. The step that
# cuts an archive has to run after the step that staples the bundle, or it packs
# a build whose ticket has just been thrown away — and an archive nobody can
# open looks exactly like one that works until somebody else downloads it.
#
# Everything the app needs is inside it: the levels, the font and the nineteen
# music tracks are embedded in the executable, every sound effect is synthesized at
# startup, and SDL travels in Contents/Frameworks. The one thing a user's Mac has to supply is macOS itself.
#
# Signing is not a step you can skip. An unsigned or ad-hoc app is precisely what
# Gatekeeper stops, so this refuses to produce one: no Developer ID certificate
# in the keychain is an error, not a fallback. Notarization is Apple scanning the
# signed build and issuing a ticket; stapling writes that ticket into the bundle
# so the first launch needs no network. Both the .app and the .dmg are notarized
# and stapled, because the player may double-click either and Gatekeeper checks
# whichever they got.
#
# One-time setup (notarytool keeps the credentials in the keychain):
#
#   xcrun notarytool store-credentials breakout-notary \
#       --apple-id you@example.com --team-id XXXXXXXXXX \
#       --password <app-specific-password from appleid.apple.com>
#
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
dist=${DIST_DIR:-$root/dist}
vendor=${VENDOR_DIR:-$root/vendor}
min_version=${MACOS_MIN_VERSION:-11.0}
profile=${NOTARY_PROFILE:-breakout-notary}
jobs=${JOBS:-$( (sysctl -n hw.ncpu 2>/dev/null || echo 4) )}

# shellcheck source=packaging/sdl3_pins.sh
source "$root/packaging/sdl3_pins.sh"

# The name, the version and the bundle id are read out of src/version.h, and the
# three frameworks out of the pins file. build_windows.sh and build_linux.sh need
# the same strings for their own payloads, and a version number written down in
# three scripts is three version numbers — the one that goes stale being the one
# nobody was looking at.
define() { sed -n "s/^#define $1 \"\(.*\)\"/\1/p" "$root/src/version.h"; }
app_name=$(define BREAKUP_APP_NAME)
version=$(define BREAKUP_VERSION)
bundle_id=${BUNDLE_ID:-$(define BREAKUP_BUNDLE_ID)}
# The range LICENSE states, with this year as its end so the bundle does not
# fall behind it; the start is the one LICENSE names.
copyright="Copyright © 2023-$(date +%Y) Robert Libšanský. MIT licensed."
frameworks=("${SDL3_LIBS[@]}")

[ -n "$app_name" ] && [ -n "$version" ] && [ -n "$bundle_id" ] || {
    echo "build_macos: could not read the app name, version and bundle id" >&2
    echo "             out of src/version.h" >&2
    exit 1
}

# CFBundleVersion has to rise with every build you hand anybody; the commit
# count is a number that already does.
build_number=$(git -C "$root" rev-list --count HEAD 2>/dev/null || echo 1)

# ---- SDL, once per pin ----------------------------------------------------
"$root/packaging/fetch_sdl3.sh" "$vendor"

# ---- the game, both slices ------------------------------------------------
# `make` on its own links Homebrew's SDL3 — arm64 only, built for this Mac's
# macOS. The shipped build overrides the flags to link the vendored universal
# frameworks instead, and writes the rpath that finds them inside the bundle.
#
# `embed` on its own first. The makefile now orders this itself — every object
# has an order-only prerequisite on it — and this line is belt and braces: it
# also means a failure to generate the header is reported as itself rather than
# as a compile error thirty lines further down.
exe=$root/build/app/breakup
make -C "$root" embed
MACOSX_DEPLOYMENT_TARGET=$min_version make -C "$root" -j"$jobs" \
    BUILD_DIR=build/app TARGET=build/app/breakup \
    CFLAGS="-std=c17 -Wall -Wextra -Wpedantic -O2 -DNDEBUG \
        -arch arm64 -arch x86_64 -F$vendor" \
    LDFLAGS="-arch arm64 -arch x86_64 -F$vendor \
        ${frameworks[*]/#/-framework } -lm \
        -Wl,-rpath,@executable_path/../Frameworks" \
    build/app/breakup

[ -x "$exe" ] || { echo "build_macos: no binary at $exe" >&2; exit 1; }

# ---- the bundle -----------------------------------------------------------
app="$dist/$app_name.app"
contents="$app/Contents"
rm -rf "$app"
mkdir -p "$contents/MacOS" "$contents/Frameworks" "$contents/Resources"

cp "$exe" "$contents/MacOS/$app_name"
chmod 755 "$contents/MacOS/$app_name"
strip -x "$contents/MacOS/$app_name"

# --- SDL, travelling inside the bundle -------------------------------------
# Each framework's install name is @rpath/<name>.framework/Versions/A/<name> and
# the link step already wrote @executable_path/../Frameworks into the binary, so
# there is nothing to rewrite here — the bundle is simply where the rpath says to
# look. Headers are dropped: megabytes of .h files are not something a player
# needs a copy of, and the framework's own signature sealed them, so its
# _CodeSignature has to go with them. We re-sign every one below anyway.
for name in "${frameworks[@]}"; do
    ditto "$vendor/$name.framework" "$contents/Frameworks/$name.framework"
    rm -rf "$contents/Frameworks/$name.framework/Headers" \
           "$contents/Frameworks/$name.framework/Versions/A/Headers" \
           "$contents/Frameworks/$name.framework/Versions/A/Resources/CMake" \
           "$contents/Frameworks/$name.framework/Versions/A/_CodeSignature"
done

# --- the wrapping ----------------------------------------------------------
sed -e "s|@NAME@|$app_name|g" \
    -e "s|@EXECUTABLE@|$app_name|g" \
    -e "s|@BUNDLE_ID@|$bundle_id|g" \
    -e "s|@VERSION@|$version|g" \
    -e "s|@BUILD@|$build_number|g" \
    -e "s|@MIN_VERSION@|$min_version|g" \
    -e "s|@COPYRIGHT@|$copyright|g" \
    "$root/packaging/Info.plist.in" > "$contents/Info.plist"
printf 'APPL????' > "$contents/PkgInfo"

# --- signing ---------------------------------------------------------------
identity=${CODESIGN_IDENTITY:-}
if [ -z "$identity" ]; then
    # A Mac signed into two teams has two Developer ID certificates, and
    # silently taking the first one ships a build under whichever name sorted
    # earliest. Say which ones there are and let CODESIGN_IDENTITY decide.
    found=$(security find-identity -v -p codesigning 2>/dev/null \
        | sed -n 's/.*"\(Developer ID Application: [^"]*\)".*/\1/p')
    count=$(printf '%s' "$found" | grep -c . || true)
    if [ "$count" -gt 1 ]; then
        echo "sign: more than one Developer ID Application certificate:"
        printf '%s\n' "$found" | sed 's/^/        /'
        echo "      pick one with: CODESIGN_IDENTITY=\"...\" make mac"
        exit 1
    fi
    identity=$found
fi

if [ -z "$identity" ]; then
    cat >&2 <<'MSG'
build_macos: no Developer ID Application certificate in the keychain.

That is not the "Apple Development" certificate Xcode makes for running on your
own devices — Apple notarizes nothing else, and an app signed with anything else
is what Gatekeeper refuses on every Mac but this one. It needs a paid Apple
Developer Program membership, and it is created in Xcode > Settings > Accounts >
(your team) > Manage Certificates > + > Developer ID Application, or on
developer.apple.com under Certificates.
MSG
    exit 1
fi
echo "sign: $identity"
team_id=$(printf '%s' "$identity" | sed -n 's/.*(\([A-Z0-9]*\))$/\1/p')

# --options runtime is the hardened runtime, which notarization requires.
# --timestamp asks Apple's timestamp server, which is what keeps the signature
# valid after the certificate itself expires.
#
# Inside out: a signature covers everything below it, so a framework signed after
# the app invalidates the app's own seal.
sign=(codesign --force --timestamp --options runtime --sign "$identity")
for name in "${frameworks[@]}"; do
    "${sign[@]}" "$contents/Frameworks/$name.framework/Versions/A"
done
"${sign[@]}" "$app"

codesign --verify --strict --verbose=2 "$app" 2>&1 | sed 's/^/verify: /'

echo
echo "$app"
echo "  version   $version ($build_number), bundle id $bundle_id"
echo "  arch      $(lipo -archs "$contents/MacOS/$app_name")"
echo "  min macOS $min_version"
echo "  size      $(du -sh "$app" | awk '{print $1}')"

# ---- Apple ----------------------------------------------------------------
submit() {
    # notarytool --wait exits non-zero when the ticket is refused, but the reason
    # is only in the log, so fetch it rather than leaving a bare failure.
    local what=$1 out
    out=$(mktemp "${TMPDIR:-/tmp}/notary.XXXXXX")
    if xcrun notarytool submit "$what" --keychain-profile "$profile" --wait 2>&1 | tee "$out"; then
        rm -f "$out"
        return 0
    fi

    # The credentials are a one-time keychain profile, and notarytool's own
    # message names the command without any of the arguments it needs. The team
    # id is in the certificate we just signed with, so fill the line in.
    if grep -q "No Keychain password item found" "$out"; then
        cat <<MSG

build_macos: no credentials stored under the profile "$profile".

Store them once — the password is an app-specific password, made at
appleid.apple.com > Sign-In and Security > App-Specific Passwords, and not your
Apple ID password:

  xcrun notarytool store-credentials $profile \\
      --apple-id <your-apple-id-email> \\
      --team-id $team_id \\
      --password <app-specific-password>

Then: make mac

The credentials belong to an Apple ID rather than to an app, so a profile stored
for another project works here too:

  NOTARY_PROFILE=<other-profile> make mac
MSG
        rm -f "$out"
        return 1
    fi

    local id
    id=$(sed -n 's/^ *id: \([0-9a-f-]*\)$/\1/p' "$out" | head -1)
    if [ -n "$id" ]; then
        echo
        echo "build_macos: Apple refused it. The reasons:"
        xcrun notarytool log "$id" --keychain-profile "$profile" || true
    fi
    rm -f "$out"
    return 1
}

# --- the app ---------------------------------------------------------------
# A .app is a directory and the upload has to be a single file: ditto's zip is
# the one that preserves the symlinks inside the frameworks, and the signature
# with them.
zip="$dist/$app_name-$version-upload.zip"
rm -f "$zip"
# An upload envelope, not an artifact: it goes whether or not the rest of this
# succeeds, so a failed run leaves dist/ holding only things worth having. The
# DMG staging tree below goes with it, for the same reason.
trap 'rm -rf "$zip" "$dist/.dmg-staging"' EXIT
ditto -c -k --keepParent "$app" "$zip"
echo
echo "build_macos: submitting $app_name.app"
submit "$zip"
xcrun stapler staple "$app"

# --- the disk image --------------------------------------------------------
# Built from the stapled app, so what the player drags across already carries its
# ticket even before the DMG's own is checked.
staging="$dist/.dmg-staging"
rm -rf "$staging"
mkdir -p "$staging"
ditto "$app" "$staging/$app_name.app"
ln -s /Applications "$staging/Applications"

dmg="$dist/$app_name-$version.dmg"
rm -f "$dmg"
hdiutil create -volname "$app_name $version" -srcfolder "$staging" \
    -ov -format UDZO -quiet "$dmg"
rm -rf "$staging"

codesign --force --timestamp --sign "$identity" "$dmg"
echo
echo "build_macos: submitting $(basename "$dmg")"
submit "$dmg"
xcrun stapler staple "$dmg"

# --- the archive -----------------------------------------------------------
# `ditto` rather than `zip`, for the reason fetch_sdl3.sh uses it: a .app is
# symlinks and resource forks, and an archiver that flattens either hands
# somebody a bundle that will not launch and cannot be diagnosed from the
# outside. It holds Breakup.app and nothing else — anything added *inside* it
# would be a file the signature does not cover, which is to say a broken
# signature on the platform that refuses to start those.
archive="$dist/$app_name-$version-macos.zip"
rm -f "$archive"
ditto -c -k --sequesterRsrc --keepParent "$app" "$archive"

# --- what Gatekeeper now says ----------------------------------------------
echo
spctl --assess --type exec --verbose=2 "$app" 2>&1 | sed 's/^/gatekeeper: /'
spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg" 2>&1 | sed 's/^/gatekeeper: /'
xcrun stapler validate "$app" | sed 's/^/staple: /'
xcrun stapler validate "$dmg" | sed 's/^/staple: /'

echo
echo "$archive"
echo "  $(du -sh "$archive" | awk '{print $1}') — notarized, stapled, send this."
echo "$dmg"
echo "  $(du -sh "$dmg" | awk '{print $1}') — the same build as a disk image."
