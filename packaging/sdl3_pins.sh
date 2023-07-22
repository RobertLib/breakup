#!/bin/bash
#
# The three SDL libraries this game is built against, pinned by version and by
# the hash of every archive any of the three platform builds downloads.
#
# It is sourced, not run: packaging/fetch_sdl3.sh reads the .dmg hashes,
# packaging/build_windows.sh the mingw ones, packaging/build_linux.sh only the
# versions — it builds from the release tag instead, because SDL_ttf needs a
# submodule no source tarball carries.
#
# One file for the same reason a shipped binary has to be traceable to the
# library it was linked against: a version written down in three scripts is
# three versions, and the one that drifts is the one nobody was looking at. It
# is also the only place to change when a pin moves.
#
# The hashes are of the release assets at
# https://github.com/libsdl-org/<repo>/releases/tag/release-<version>
# and are what makes "downloaded from the internet" into "this exact library":
#
#   .dmg          SDL3-<v>.dmg                       universal macOS frameworks
#   mingw         SDL3-devel-<v>-mingw.tar.gz        import lib + DLL for x86_64
#
# SDL_mixer's release also carries optional/ — gme, ogg, opus, wavpack, xmp.
# None of them is fetched, and every decoder that would need one is switched off
# in the Linux build. The one format this game does ship — Ogg Vorbis, for the
# nineteen embedded music tracks — is decoded by stb_vorbis, which is inside
# SDL_mixer itself and needs no library at all. See src/lib/audio.c.

# repo | framework/lib name | version | sha256 of the .dmg | sha256 of the mingw .tar.gz
SDL3_PINS=(
    "SDL|SDL3|3.4.14|bae77509ccddcc7a443bb09730ab854c976e8f8bcf57b66d6bad6af2e17f38c2|daad6044f75689ea5edb34eed0cc6dccac85246a3f469714d46da3a178ea7edc"
    "SDL_ttf|SDL3_ttf|3.2.2|2c7035eba9e63df137dbe95d2672f4c52e68d76bf6dd7c47a325f29bb6ba3cf7|bb57f26787d6a2e108158562feb061fcdf6f68a110f9c8cf9af42ff343d4e41c"
    "SDL_mixer|SDL3_mixer|3.2.4|78a12346d8492c62006fce2fcc6b8a534d87fb5f4aa7e125229898ba506c69ac|2ff5f59be4683e0afb4339c4fa73c6228651e52d37b040ecf80da08edb3cecd6"
)

# The names, in link order, for the scripts that need to name the libraries
# without caring which version they are at.
SDL3_LIBS=(SDL3 SDL3_ttf SDL3_mixer)

# sdl3_pin <name> <field>, where field is 3 for the version, 4 for the .dmg
# hash and 5 for the mingw one. Prints nothing and fails if the name is not
# pinned, which is the only useful thing to do with a typo in a hash lookup.
sdl3_pin() {
    local want=$1 field=$2 row
    for row in "${SDL3_PINS[@]}"; do
        IFS='|' read -r _ name _ _ _ <<< "$row"
        if [ "$name" = "$want" ]; then
            printf '%s' "$row" | cut -d'|' -f"$field"
            return 0
        fi
    done
    echo "sdl3_pins: $want is not pinned" >&2
    return 1
}
