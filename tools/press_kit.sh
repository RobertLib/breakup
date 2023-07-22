#!/bin/bash
#
# Photograph the game: seventeen stills, four animations, the itch.io cover and a
# wallpaper, into dist/press/ with a MANIFEST.txt naming what each file is and
# the command that produced it. It also refreshes docs/screenshot.png, which is
# the only thing it writes outside that directory - see the end of the file.
#
# It exists because there is no art in this repository to crop. Every pixel is
# drawn at runtime and the levels are text files, so the only place a picture of
# this game exists is the back buffer of a running process - which is what
# BREAKUP_SHOT reads (see src/main.c). That also means the pictures can be
# *rebuilt* after a change to the art rather than re-photographed by hand and
# quietly left a version behind.
#
# The game plays itself here. BREAKUP_AUTOPLAY makes the paddle track the
# lowest ball, so these are captures of real play - bricks break, power-ups
# drop, the score climbs and the animations move because the game is moving,
# not because the background is. Autoplay only steers, though: the ball is
# launched by an injected Space (BREAKUP_KEYS, scancode 44), and without that
# every shot would be a picture of a ball sitting on the paddle.
#
# Every run here is a *scripted* one, which is the part that makes a capture a
# measurement rather than a photograph of this machine: no save file is read or
# written, so the pictures do not inherit the settings of whoever runs it and
# nobody's progress or high scores are overwritten, and the world is stepped by
# a fixed 1/FPS rather than by the wall clock. With BREAKUP_SEED on top of
# that, two runs of one commit produce byte-identical files - which is what
# makes a still diffable and a regression in one visible.
#
# Needs ImageMagick for everything past the capture itself: the game writes
# BMPs, and a store page wants PNGs, a cover at a fixed size and a GIF or two.
# On a Mac `sips` covers the conversion alone, and the script says what it could
# not produce without ImageMagick rather than quietly leaving it out.
set -uo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
binary=${PRESS_BINARY:-$root/breakup}
out=${1:-$root/dist/press}

# Pinned, so that the same commit photographs the same game twice: the
# starfields, the particles, the power-up drops and the enemy paths all come out
# of the RNG. The value is arbitrary and overridable - somebody re-cutting the
# store page may simply prefer a different arrangement of a level. What matters
# is that it is the same one twice.
seed=${PRESS_SEED:-20250824}

# How long each animation runs and how it is sampled. The step is in game
# frames, so a step of 3 against FPS 60 is a GIF at 20 - one variable rather
# than two numbers, because the delay a GIF is assembled with and the interval
# the frames were taken at have to agree or the animation plays at a speed the
# game does not run at.
gif_frames=${PRESS_GIF_FRAMES:-60}
gif_step=${PRESS_GIF_STEP:-3}

# Half of the frame, and the one place here that trades quality for bytes.
# itch.io stops autoplaying a GIF at somewhere around 3 MB and these are three
# seconds of a moving starfield, which is the worst case there is for GIF's
# between-frame compression: at full size they land past 4 MB and stop moving on
# the page, which makes them worse than a still rather than better. 50% is an
# exact halving of every dimension, so it is the cleanest scale available.
gif_scale=${PRESS_GIF_SCALE:-50%}

# How different two pixels may be and still count as unchanged between frames;
# see the note on animation() below.
gif_fuzz=${PRESS_GIF_FUZZ:-2%}

case $binary in
    /*) ;;
    *) binary=$(cd "$(dirname "$binary")" && pwd)/$(basename "$binary") ;;
esac
if [ ! -x "$binary" ]; then
    echo "press: no executable at $binary (run: make release)" >&2
    exit 1
fi

# ImageMagick 7 calls itself `magick`, 6 calls itself `convert`, and macOS has
# `sips` whatever happens. The first two can do all of this; the third can only
# convert, so what it cannot produce is named rather than silently missing.
im=""
if command -v magick >/dev/null 2>&1; then
    im=magick
elif command -v convert >/dev/null 2>&1; then
    im=convert
fi
if [ -z "$im" ] && ! command -v sips >/dev/null 2>&1; then
    echo "press: needs ImageMagick (magick/convert) or macOS sips" >&2
    exit 1
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/breakup-press.XXXXXX") || work=""
if [ -z "$work" ]; then
    echo "press: could not create a temporary directory in ${TMPDIR:-/tmp}" >&2
    exit 1
fi
log=$work/run.log

# Headless, on the software renderer, so this needs no window and no audio
# device: a busy or absent one must not be the reason a picture is missing. It
# is also what fixes the size of the captured frame - the dummy driver opens the
# window the game asks for, so the frame comes back at exactly the logical size
# checked for below.
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy

# The frame's own size, off the headers that define it rather than written down
# here, and asserted against every capture. The iOS branch of SCREEN_HEIGHT
# comes first in globals.h, so the desktop one is the last match.
#
# `[0-9][0-9]*` rather than `[0-9]*`: the latter matches a define whose value is
# another macro too, capturing nothing, and `tail -1` then prefers that empty
# string over a real number further up the file. Requiring a digit means a
# define that has become an alias is not a match at all, and the guard below
# names it rather than a crop quietly losing its offset.
view_w=$(sed -n 's/^#define SCREEN_WIDTH \([0-9][0-9]*\).*/\1/p' "$root/src/globals.h" | tail -1)
view_h=$(sed -n 's/^#define SCREEN_HEIGHT \([0-9][0-9]*\).*/\1/p' "$root/src/globals.h" | tail -1)
fps=$(sed -n 's/^#define FPS \([0-9][0-9]*\).*/\1/p' "$root/src/globals.h" | tail -1)
# The height of the HUD panel, which the cover is cropped below. globals.h owns
# this number - it is the world's ceiling as well as the panel, so three files
# measure from it. src/ui/status-bar.c keeps only a PANEL_HEIGHT alias for it,
# and reading that alias is what broke this script when the number moved.
panel=$(sed -n 's/^#define STATUS_BAR_HEIGHT \([0-9][0-9]*\).*/\1/p' "$root/src/globals.h" | tail -1)
if [ -z "$view_w" ] || [ -z "$view_h" ] || [ -z "$fps" ] || [ -z "$panel" ]; then
    echo "press: could not read SCREEN_WIDTH/SCREEN_HEIGHT/FPS/STATUS_BAR_HEIGHT" \
         "from src/globals.h" >&2
    exit 1
fi

# Every level file is a level, so this is the level count without a second place
# to keep it. The level select screen is the one screen a scripted run cannot
# photograph honestly without it: it starts from the shipped save, where exactly
# one level is unlocked, so the grid would be twenty-six padlocks and no names.
levels=$(find "$root/assets/levels" -name 'level*.txt' | wc -l | tr -d ' ')

# Injected key presses, by USB scancode, which is what BREAKUP_KEYS speaks.
# 44 is Space - the launch, and afterwards the trigger for lasers and for
# releasing a caught ball. Several of them, because one launch only covers the
# first ball of the shot: a lost ball has to be served again or the rest of the
# capture is a picture of an empty paddle.
launch=150:44,500:44,900:44,1300:44,1700:44,2100:44
esc=41    # Escape - the pause menu
enter=40  # Enter - selects a menu item
down=81   # Down - moves through a menu

complaints='could not|Could not|Unable to|error:|ERROR|AddressSanitizer|SUMMARY: .*Sanitizer'

failures=0
taken=0

mkdir -p "$out"
manifest=$out/MANIFEST.txt
{
    echo "# What each file here is, and the command that produced it."
    echo "#"
    echo "# Rebuilt by tools/press_kit.sh against $(cd "$root" && git rev-parse --short HEAD 2>/dev/null || echo 'an untracked tree')."
    echo "# Every frame is captured headless on the dummy video driver, at the game's"
    echo "# logical frame (${view_w}x${view_h}) and the settings it ships with: BREAKUP_SHOT"
    echo "# makes the run a scripted one, so nothing on the disk of whoever ran this"
    echo "# reached the pictures and nothing here reached theirs. The commands below"
    echo "# reproduce the files exactly, byte for byte, from this commit."
    echo
} >"$manifest"

# One capture: run the game with the environment given, then say so or say why
# not.
#
# The checks are the point of it. The failure modes are a process that dies
# before it writes, a build that ignored the switch and drew one frame, a frame
# that came back at the wrong size because something else decided how big the
# window was - and a press run that quietly produced nothing is how a store page
# ends up showing the previous version.
capture()
{
    local name=$1 frame=$2 frames=$3 step=$4
    shift 4

    local status=0
    env BREAKUP_SEED="$seed" \
        BREAKUP_SHOT="$frame:$work/$name.bmp" \
        BREAKUP_SHOT_FRAMES="$frames" \
        BREAKUP_SHOT_STEP="$step" \
        "$@" "$binary" >"$log" 2>&1 || status=$?

    if [ "$status" -ne 0 ]; then
        echo "press: $name exited $status"
        sed 's/^/       /' "$log"
        failures=$((failures + 1))
        return 1
    fi
    if grep -qE "$complaints" "$log"; then
        echo "press: $name had something to say"
        grep -E "$complaints" "$log" | sed 's/^/       /'
        failures=$((failures + 1))
        return 1
    fi

    local written
    written=$(grep -c '^Wrote ' "$log")
    if [ "$written" -ne "$frames" ]; then
        echo "press: $name wrote $written frame(s) of $frames; BREAKUP_SHOT was not honoured"
        sed 's/^/       /' "$log"
        failures=$((failures + 1))
        return 1
    fi

    local sizes
    sizes=$(grep -o '([0-9]*x[0-9]*)' "$log" | sort -u | tr -d '()' | tr '\n' ' ')
    if [ "$sizes" != "${view_w}x${view_h} " ]; then
        echo "press: $name came out at $sizes, not ${view_w}x${view_h}"
        failures=$((failures + 1))
        return 1
    fi

    taken=$((taken + written))
    return 0
}

# The command that reproduces a file, written into the MANIFEST beside it. It is
# a promise, so it carries the seed: without it the same line produces a
# different picture.
recipe()
{
    local frame=$1 name=$2 frames=$3 step=$4
    shift 4

    local burst=""
    if [ "$frames" -gt 1 ]; then
        burst=" BREAKUP_SHOT_FRAMES=$frames BREAKUP_SHOT_STEP=$step"
    fi

    echo "\$ BREAKUP_SEED=$seed $* BREAKUP_SHOT=$frame:$name.bmp$burst ./breakup"
}

# A still, at native size and again at twice it.
#
# Both, because they answer different questions. itch.io wants a screenshot no
# wider than 1920 and shows it in a gallery a fraction that size, so the doubled
# one is what looks sharp there; the native one is the game pixel for pixel, and
# it is the one to attach to a bug report. The double is a point upscale -
# nearest neighbour - because it is then exactly the same picture and not a
# filter's opinion of it.
still()
{
    local name=$1 frame=$2 caption=$3
    shift 3

    capture "$name" "$frame" 1 1 "$@" || return

    if [ -n "$im" ]; then
        "$im" "$work/$name.bmp" -strip "$out/$name.png"
        "$im" "$work/$name.bmp" -filter point -resize 200% -strip \
            "$out/$name@2x.png"
    else
        sips -s format png "$work/$name.bmp" --out "$out/$name.png" >/dev/null
    fi

    printf '%-26s %s\n%-26s   %s\n\n' \
        "$name.png" "$caption" "" "$(recipe "$frame" "$name" 1 1 "$@")" >>"$manifest"
    echo "press: $name.png"
}

# A burst, assembled into a GIF.
#
# The frames are taken every `gif_step` game frames and played back at
# FPS/gif_step, so the animation runs at the speed the game runs at. `-layers
# optimize` is what keeps three seconds of this inside the few megabytes a store
# page will autoplay - see the note on gif_scale above for why that is tight.
#
# `-fuzz` is the other half of that, and it became the load-bearing half once
# the field started blooming. `-layers optimize` only drops a pixel it can prove
# has not changed, and against a bloom every pixel has changed: a glow moving
# across the nebula shifts the whole gradient under it by a value or two, which
# is invisible and which the optimizer has to keep. That took these from 2.4MB
# to 3.1MB, past the point where the store page stops playing them. A couple of
# percent of slack is below what the eye finds on a 400x300 frame at 20fps and
# takes about four fifths off the file.
#
# Not `-colors`: quantizing the sequence to one palette was the obvious guess
# and measured as nothing at all, because ImageMagick is already writing one.
animation()
{
    local name=$1 frame=$2 caption=$3
    shift 3

    if [ -z "$im" ]; then
        echo "press: skipping $name.gif; that needs ImageMagick"
        return
    fi

    capture "$name" "$frame" "$gif_frames" "$gif_step" "$@" || return

    local rate=$((fps / gif_step))
    if [ $((rate * gif_step)) -ne "$fps" ]; then
        echo "press: $name.gif plays at ${rate}fps; $fps does not divide by $gif_step"
    fi

    "$im" -delay "1x$rate" -loop 0 "$work/$name"-*.bmp -resize "$gif_scale" \
        -fuzz "$gif_fuzz" -layers optimize -strip "$out/$name.gif"

    printf '%-26s %s\n%-26s   %s\n\n' \
        "$name.gif" "$caption ($gif_frames frames at ${rate}fps, scaled $gif_scale, fuzz $gif_fuzz)" \
        "" "$(recipe "$frame" "$name" "$gif_frames" "$gif_step" "$@")" >>"$manifest"
    echo "press: $name.gif ($(du -h "$out/$name.gif" | cut -f1))"
}

# ---------------------------------------------------------------------------
# The stills.
#
# **The number is the order.** Which picture goes first is a decision somebody
# has to make and a directory listing will not make it, so it is written into the
# filenames. Which level each shot is of, and at which frame, is written here
# rather than derived: a photograph is a choice about composition, and "the
# multiball in The Vault" or "the chain going off in Furnace" is the whole point
# of it. The frames are late on purpose - the first second of any level is a full
# grid and nothing happening, and what a store page needs is the game in motion
# with a hole punched through the bricks.
#
# One shot per world, in order, then the screens. itch/README.md names the five
# the store page uses.
# ---------------------------------------------------------------------------
still 01-citadel      800 "World 1, Citadel: the ball through the gap, on an expanded paddle" \
    BREAKUP_LEVEL=6 BREAKUP_AUTOPLAY=1 BREAKUP_KEYS=$launch
still 02-honeycomb   1300 "World 2, Honeycomb: thirteen gold bricks and a caught ball" \
    BREAKUP_LEVEL=9 BREAKUP_AUTOPLAY=1 BREAKUP_KEYS=$launch
still 03-vault       1550 "Multiball in The Vault, over a gold seam and a splitter" \
    BREAKUP_LEVEL=11 BREAKUP_AUTOPLAY=1 BREAKUP_KEYS=$launch
still 04-furnace     1300 "World 3, Furnace: explosive bricks going off in a chain" \
    BREAKUP_LEVEL=14 BREAKUP_AUTOPLAY=1 BREAKUP_KEYS=$launch
still 05-stormfront  2050 "World 4, Stormfront: divers coming down at the paddle" \
    BREAKUP_LEVEL=25 BREAKUP_AUTOPLAY=1 BREAKUP_KEYS=$launch
still 06-levels        90 "Level select: twenty-seven levels across four worlds" \
    BREAKUP_STATE=levels BREAKUP_UNLOCKED=$levels
still 07-title        210 "The title screen" \
    BREAKUP_STATE=intro
still 08-bastion     1800 "Lasers, in the steel maze of Bastion" \
    BREAKUP_LEVEL=15 BREAKUP_AUTOPLAY=1 BREAKUP_KEYS=$launch
still 16-warden      1500 "The Warden, the first boss: break the ring, hit the crystal" \
    BREAKUP_LEVEL=4 BREAKUP_AUTOPLAY=1 BREAKUP_KEYS=$launch
still 17-sovereign   2600 "The Sovereign, the last boss: two rings, turning against each other" \
    BREAKUP_LEVEL=27 BREAKUP_AUTOPLAY=1 BREAKUP_KEYS=$launch
still 09-core         800 "The last of the hand-designed levels: The Core" \
    BREAKUP_LEVEL=26 BREAKUP_AUTOPLAY=1 BREAKUP_KEYS=$launch
still 10-pause        520 "The pause menu, over a live field" \
    BREAKUP_LEVEL=17 BREAKUP_AUTOPLAY=1 BREAKUP_KEYS=150:44,500:$esc
still 11-menu          90 "The main menu" \
    BREAKUP_STATE=menu
still 12-options      150 "Options: music, sound and fullscreen, and they persist" \
    BREAKUP_STATE=menu BREAKUP_KEYS=60:$down,64:$down,68:$down,72:$enter
# In the kit because it is the only picture of a full combo cascade, and not one
# of the store page's five because at gallery size it is a wall of floating
# numbers. It is also the shot that found the HUD's one layout bug: the combo
# badge used to print on top of the level name at six digits, which is exactly
# when a cascade is running (see drawStatusBar in src/ui/status-bar.c).
still 13-nebula       800 "A five-times combo cascade in Nebula, at 500 a brick" \
    BREAKUP_LEVEL=21 BREAKUP_AUTOPLAY=1 BREAKUP_KEYS=$launch
still 14-gameover     150 "Game over, on the first level" \
    BREAKUP_STATE=gameover BREAKUP_SCORE=2450
# SPOILER. It is the last screen of the game and it is in here because a press
# kit is not only a store page - a review, a devlog and a trailer's last second
# all want it. itch/README.md says which files the store page uses, and this is
# not one of them.
still 15-win          200 "SPOILER: the last screen of the game" \
    BREAKUP_STATE=win BREAKUP_SCORE=68400

# ---------------------------------------------------------------------------
# The animations.
#
# One per world, and they are real play rather than an idling background:
# BREAKUP_AUTOPLAY steers the paddle and the injected Space serves the ball, so
# what moves in these is the game being played. Which is the whole reason this
# game can have GIFs at all - a headless capture receives no input, and a
# brick-breaker with nobody on the paddle is thirty seconds of a ball falling
# past it.
#
# The start frames are chosen the same way the stills are: far enough in that
# there is a hole in the bricks and something in the air.
# ---------------------------------------------------------------------------
animation 30-loop-citadel     700 "World 1: the classic, in motion" \
    BREAKUP_LEVEL=6 BREAKUP_AUTOPLAY=1 BREAKUP_KEYS=$launch
animation 31-loop-vault      1400 "Three balls at once in The Vault" \
    BREAKUP_LEVEL=11 BREAKUP_AUTOPLAY=1 BREAKUP_KEYS=$launch
animation 32-loop-furnace    1150 "A chain of explosive bricks, and what drops out of it" \
    BREAKUP_LEVEL=14 BREAKUP_AUTOPLAY=1 BREAKUP_KEYS=$launch
animation 33-loop-stormfront 1900 "Four divers, one paddle" \
    BREAKUP_LEVEL=25 BREAKUP_AUTOPLAY=1 BREAKUP_KEYS=$launch

# ---------------------------------------------------------------------------
# The derived artwork.
#
# itch.io asks for one image at a fixed shape - 630x500, shown as small as
# 315x250 - and it is the only picture most people will ever see of this game.
# The title screen is the obvious candidate and the wrong one: at 315x250 it is
# a small logotype in three quarters of an empty nebula, in a gallery of covers
# built to be read at exactly that size. So the cover is the game instead, cut
# out of the Citadel still below the HUD - a bar reading SCORE 001500 is a
# screenshot's furniture and a cover has no use for it - which leaves rows of
# neon bricks, a ball with its trail and the paddle. That says brick-breaker
# from across the page, which is the entire job.
#
# Which frame it comes from is one line, and 09-core (denser, purple) is the
# alternative worth trying if this one ever stops looking right.
#
# Resized with a smooth filter rather than the point-then-Lanczos a pixel-art
# cover needs: nothing here is drawn on a pixel grid - the bricks are gradients,
# the ball is a glow and the text is anti-aliased - so nearest neighbour would
# only add stair-steps to curves that never had them.
# ---------------------------------------------------------------------------
if [ -n "$im" ] && [ -f "$work/01-citadel.bmp" ]; then
    cover_h=$((view_h - panel))
    cover_w=$((cover_h * 630 / 500))
    cover_x=$(((view_w - cover_w) / 2))

    "$im" "$work/01-citadel.bmp" \
        -crop "${cover_w}x${cover_h}+${cover_x}+${panel}" +repage \
        -filter Lanczos -resize 1260x1000\! -strip "$out/cover-630x500@2x.png"
    "$im" "$out/cover-630x500@2x.png" -filter Lanczos -resize 630x500\! -strip \
        "$out/cover-630x500.png"

    printf '%-26s %s\n%-26s   # 01-citadel, cropped %s and resized\n\n' \
        "cover-630x500.png" "itch.io cover art (and @2x)" \
        "" "${cover_w}x${cover_h}+${cover_x}+${panel}" >>"$manifest"
    echo "press: cover-630x500.png"
fi

# A page background, and the one file here that is a wallpaper rather than a
# document: 16:9 out of a 4:3 frame, so it is the title screen's middle band with
# the sky above the logotype kept and the empty floor below it trimmed.
#
# Its own capture, at a frame where PRESS ANY KEY has blinked off, because a page
# background that tells the reader to press a key is a page background with a
# mistake in it.
if [ -n "$im" ] && capture wallpaper 390 1 1 BREAKUP_STATE=intro; then
    band=$((view_w * 9 / 16))
    "$im" "$work/wallpaper.bmp" \
        -crop "${view_w}x${band}+0+$(((view_h - band) / 2 - 40))" +repage \
        -filter Lanczos -resize 1920x1080\! -strip "$out/wallpaper-1920x1080.png"

    printf '%-26s %s\n%-26s   %s\n\n' \
        "wallpaper-1920x1080.png" "16:9 page background, from the title screen" \
        "" "$(recipe 390 wallpaper 1 1 BREAKUP_STATE=intro)" >>"$manifest"
    echo "press: wallpaper-1920x1080.png"
fi

# The picture at the top of README.md, which is a copy of a still rather than a
# capture of its own.
#
# It was taken by hand once and had been stale ever since: an older HUD with the
# score running into the level name, and four power-up captions printed on top of
# each other. Derived here, it is whatever the game actually looks like as of the
# last time anybody ran this, which is the same argument as for everything else in
# this file. Furnace, because that is the level the hand-taken one was of, and the
# doubled copy, because GitHub renders it about 800 wide and a high-density
# display then has two pixels for each of the game's.
#
# Only when this run is the real one. A press kit cut into a scratch directory to
# be compared against another has no business editing the repository, and finding
# out that it did by reading `git status` afterwards is the kind of surprise a
# tool gets uninstalled for.
if [ -n "$im" ] && [ -f "$out/04-furnace@2x.png" ] && [ -d "$root/docs" ]; then
    if [ "$out" = "$root/dist/press" ]; then
        cp "$out/04-furnace@2x.png" "$root/docs/screenshot.png"
        printf '%-26s %s\n\n' "(docs/screenshot.png)" \
            "a copy of 04-furnace@2x.png, for README.md" >>"$manifest"
        echo "press: docs/screenshot.png (copied from 04-furnace@2x.png)"
    else
        echo "press: leaving docs/screenshot.png alone; this run is not the default output"
    fi
fi

if [ "$failures" -ne 0 ]; then
    echo "press: $failures capture(s) failed"
    rm -rf "$work"
    exit 1
fi

rm -rf "$work"
echo "press: $taken frames into $out - see $manifest"
if [ -z "$im" ]; then
    echo "press: no ImageMagick, so no @2x, no cover, no wallpaper and no GIFs;" \
         "install it and run this again for those"
fi
