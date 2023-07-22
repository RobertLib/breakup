TARGET := breakup
SRC_DIR := src
BUILD_DIR := build

# The generated header carrying the font, the levels and the music. Named here
# because three rules below need to agree on it: what builds it, what waits for
# it, and what `clean` removes.
EMBEDDED_HEADER := $(SRC_DIR)/embedded_assets.h

UNAME_S := $(shell uname -s)
SDL_PKGS := sdl3 sdl3-ttf sdl3-mixer

# `=` rather than `:=`, so pkg-config runs only when a build actually asks for
# these. The three platform scripts in packaging/ pass CFLAGS and LDFLAGS whole
# on the command line, where they override even a target-specific assignment,
# and neither a mingw cross-build nor a Linux one has a pkg-config that knows
# about this machine's SDL3. Immediate assignment ran it regardless — on every
# invocation of every target, including `make clean` — and printed its complaint
# into builds with no use for the answer.
#
# Lazy, but once: a plain `=` re-ran pkg-config on every expansion, which is
# every compile line, so a build of thirty objects asked the same question
# thirty times. The `$(eval ...)` turns the variable into a `:=` the first time
# it is read and hands back that answer from then on.
PKG_CFLAGS = $(eval PKG_CFLAGS := $$(shell pkg-config --cflags $$(SDL_PKGS)))$(PKG_CFLAGS)
PKG_LIBS = $(eval PKG_LIBS := $$(shell pkg-config --libs $$(SDL_PKGS)))$(PKG_LIBS)
CFLAGS_BASE = -std=c17 -Wall -Wextra -Wpedantic $(PKG_CFLAGS)
LDFLAGS_SDL = $(PKG_LIBS)

# The development link line, and the one place in this file that is about a
# platform. `-headerpad_max_install_names` and the Homebrew rpaths are ld64
# flags: GNU ld rejects the first outright, so a Linux developer running plain
# `make` got a link error about a flag that has nothing to do with their machine.
ifeq ($(UNAME_S),Darwin)
# `?=` cannot do this: make defines CC itself, so its origin is `default` rather
# than undefined and `?=` leaves it alone — which is why this said clang and
# built with cc. Elsewhere make's own `cc` is the right answer anyway, and the
# cross-build passes CC on the command line.
ifeq ($(origin CC),default)
CC := clang
endif
# Homebrew's own .pc files already carry an rpath to wherever they were
# installed, so adding both of these unconditionally handed ld64 the same
# -rpath twice and it said so on every single link. `filter-out` drops whichever
# one pkg-config has already asked for and keeps the other, which is the point
# of naming both: /usr/local for an Intel Homebrew, /opt/homebrew for an
# Apple-silicon one, and neither is guaranteed to be the one that answered.
MAC_RPATHS := -Wl,-rpath,/usr/local/lib -Wl,-rpath,/opt/homebrew/lib
LDFLAGS = $(LDFLAGS_SDL) $(filter-out $(LDFLAGS_SDL),$(MAC_RPATHS)) \
          -Wl,-headerpad_max_install_names
else
# CC is left to make's own default of `cc`, which is the right answer on Linux;
# `?=` could not have set it anyway, per the note above.
LDFLAGS = $(LDFLAGS_SDL) -lm
endif

SOURCES := $(shell find $(SRC_DIR) -name '*.c')
OBJECTS_REL := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/release/%.o,$(SOURCES))
OBJECTS_DBG := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/debug/%.o,$(SOURCES))
OBJECTS_ASAN := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/asan/%.o,$(SOURCES))

# The tests. See the `test` target near the bottom of this file.
#
# The game's own sources are compiled again into build/test rather than reusing
# build/release, because the two are built with different flags and sharing an
# object tree between them is how a test run ends up measuring a binary nobody
# ships. main.c is the one file left out: the test runner brings its own main().
TEST_DIR := tests
TEST_SOURCES := $(shell find $(TEST_DIR) -name '*.c' 2>/dev/null)
TEST_TARGET := $(TARGET)_test
SOURCES_NO_MAIN := $(filter-out $(SRC_DIR)/main.c,$(SOURCES))
OBJECTS_TEST := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/test/src/%.o,$(SOURCES_NO_MAIN)) \
                $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/test/tests/%.o,$(TEST_SOURCES))

# Header dependencies. These go in the compile recipe rather than into CFLAGS,
# because CFLAGS is assigned per-target below and passed whole on the command
# line by the three packaging scripts — either of which would drop them, and a
# release build is exactly where a stale object matters most.
#
# Without these, an object depended on its .c file and nothing else. Editing a
# header rebuilt nothing, and the sharp edge was assets/levels/*.txt: `make`
# regenerated the embedded header, level-manager.c was not recompiled, and the
# binary went on carrying the levels from before the edit. `make clean` was the
# only honest way to build and nothing said so.
#
# -MP writes a phony target for every header, so deleting or renaming one is a
# rebuild rather than "no rule to make target".
DEPFLAGS := -MMD -MP

DEPS := $(OBJECTS_REL:.o=.d) $(OBJECTS_DBG:.o=.d) $(OBJECTS_ASAN:.o=.d) \
        $(OBJECTS_TEST:.o=.d)
-include $(DEPS)

# The generated header every object may include, as an order-only prerequisite:
# it has to exist before anything compiles, but it is not a reason to recompile
# (the depfiles above decide that). Without this, `make -j` raced embed_assets.sh
# against the compiles that include what it writes, and the packaging scripts had
# to run `make embed` on its own first to be safe.
$(OBJECTS_REL) $(OBJECTS_DBG) $(OBJECTS_ASAN) $(OBJECTS_TEST): | $(EMBEDDED_HEADER)

# And the header is a file with prerequisites rather than a phony target, which
# is the difference between a two-second no-op build and a ten-second one.
#
# `embed` used to be phony, and phony means "always out of date": every single
# invocation of make - `make`, `make clean`, a second `make` with nothing to do -
# ran embed_assets.sh, which xxd'd 18 MB of Ogg Vorbis into a 113 MB header and
# moved it into place. That gave it a new mtime whatever was in it, so the four
# translation units that include it recompiled and the binary relinked, every
# time, for a file that had not changed. Named here, make compares it against
# assets/ itself and does not run the recipe at all unless something under there
# is actually newer.
#
# ASSETS is the same set embed_assets.sh embeds. The two are written down twice
# and the cost of them disagreeing is small in both directions: a file matched
# here and not there is a rebuild that writes the same header, and one matched
# there and not here is caught by the script's own content comparison the next
# time anything else changes.
#
# `$(shell)` splits on whitespace, so an asset path must not contain a space.
ASSETS := $(shell find assets -type f \( -name '*.bmp' -o -name '*.wav' \
                       -o -name '*.ogg' -o -name '*.ttf' -o -name '*.txt' \))

# The directories as well as the files, and that is about *deleting* one.
#
# A list of files can only ever name files that exist. Removing assets/levels/
# level28.txt leaves nothing behind that is newer than the header, so make had
# nothing to do and the binary went on carrying a level that is no longer in the
# tree - until something else under assets/ happened to change, or somebody ran
# `make clean`. Nothing said so, and the game went on offering the level.
#
# A directory's mtime moves when an entry is added, removed or renamed, which is
# exactly the case the file list misses. It costs a spurious run of the script
# whenever something else touches one of these directories - a .DS_Store, say -
# and that is affordable because the script compares contents before it replaces
# anything and `touch`es the header when they match, so an unnecessary run ends
# in seconds and recompiles nothing. See the note at the bottom of
# embed_assets.sh.
ASSET_DIRS := $(shell find assets -type d)

$(EMBEDDED_HEADER): $(ASSETS) $(ASSET_DIRS) embed_assets.sh
	./embed_assets.sh

# Default to debug build
all: debug

# Debug build target
debug: CFLAGS = $(CFLAGS_BASE) -O0 -g -DDEBUG
debug: $(TARGET)_dbg
	@echo "Debug build ready: $(TARGET)_dbg"

$(TARGET)_dbg: $(OBJECTS_DBG)
	$(CC) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/debug/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# The sanitizer build: AddressSanitizer and UndefinedBehaviorSanitizer on the
# debug flags, with -O1 so that a run under them is playable and the autoplay
# in README.md's development helpers gets somewhere. Its own object tree,
# because these objects are not interchangeable with the debug ones. The link
# has to carry the same -fsanitize flags as the compile, or the runtime is not
# linked in and every instrumented call is an undefined symbol.
SANITIZE := -fsanitize=address,undefined -fno-omit-frame-pointer
sanitize: CFLAGS = $(CFLAGS_BASE) -O1 -g -DDEBUG $(SANITIZE)
sanitize: LDFLAGS += $(SANITIZE)
sanitize: $(TARGET)_asan
	@echo "Sanitizer build ready: $(TARGET)_asan"

$(TARGET)_asan: $(OBJECTS_ASAN)
	$(CC) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/asan/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Release build target
release: CFLAGS = $(CFLAGS_BASE) -O2 -DNDEBUG
release: $(TARGET)
	@echo "Release build ready: $(TARGET)"

$(TARGET): $(OBJECTS_REL)
	$(CC) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/release/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

run: debug
	./$(TARGET)_dbg

# The tests, and the target CI runs on every push.
#
# What is testable here is narrower than in most programs and worth saying out
# loud: every sprite and every sound is generated at runtime, so nearly all of
# this game needs a window, a renderer and a mixer before it does anything. The
# tests bring none of those up. What they cover is the part that does not need
# them - the arithmetic in globals.h, the level files as they actually ship,
# the world and music tables, and the save-independent half of the progression
# - which is also where every bug this suite was written after actually lived.
#
# -O1 rather than -O0: the level files are parsed for real and a debug build of
# that is slower than the tests are worth waiting for. -g stays, because a
# failure that has to be reproduced under a debugger should not need a rebuild.
test: CFLAGS = $(CFLAGS_BASE) -O1 -g
test: $(TEST_TARGET)
	@echo
	./$(TEST_TARGET)

$(TEST_TARGET): $(OBJECTS_TEST)
	$(CC) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test/src/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/test/tests/%.o: $(TEST_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Kept as the name to type, and as the name the packaging scripts use; the rule
# that does the work is the one on $(EMBEDDED_HEADER) above.
embed: $(EMBEDDED_HEADER)

# The release, and the only thing worth handing to anybody else: a universal
# .app with SDL inside it, signed with Developer ID, notarized by Apple and
# stapled. `make debug` and `make release` above are the development loop -
# they link Homebrew's SDL3 and run on this machine only.
mac:
	packaging/build_macos.sh

# The Windows payload: breakup.exe, the three SDL DLLs beside it and nothing to
# install. Cross-built with mingw-w64, which works on this Mac as well as on
# Linux — src/ includes SDL and the C standard library and stops, so what a
# Windows build needs is a compiler that targets Windows.
win:
	packaging/build_windows.sh

# The Linux payload, and the one archive that cannot be cut on this Mac: its SDL
# is compiled against the userland it will run on. Run it on a Linux box, or
# press the button on .github/workflows/payloads.yml, which is the Linux machine
# for anybody who has not got one.
linux:
	packaging/build_linux.sh

# The fourth payload, and the only one that is not a download: itch.io runs an
# HTML5 build in an iframe on the game's own page, which is how most of that site
# is actually played. It cross-builds anywhere emscripten does, a Mac included,
# and it is the one archive whose index.html has to sit at the *root* of the zip
# rather than inside a named folder - see the note at the top of
# packaging/build_web.sh.
#
# The three libraries are built from source here for one cmake flag:
# SDL_EMSCRIPTEN_PERSISTENT_PATH is what decides whether a player's settings,
# unlocked levels and high scores survive closing the tab.
web:
	packaging/build_web.sh

# `press` is not a build and not a release. Every pixel of this game is drawn at
# runtime, so the store page's screenshots are captured from the built game
# rather than kept as files - which means they can be *rebuilt* after a change to
# the art instead of being re-photographed by hand and quietly left a version
# behind. See tools/press_kit.sh, and BREAKUP_SHOT in src/main.c.
press: release
	PRESS_BINARY=$(CURDIR)/$(TARGET) tools/press_kit.sh

# vendor/ is a verified download and survives `make clean`; this refetches it,
# and `make sdlclean` below is what throws it away.
sdl3:
	packaging/fetch_sdl3.sh vendor

# The object trees this makefile and the four packaging scripts write into.
# Named one at a time rather than as `$(BUILD_DIR)`, because build/ is not all
# ours — see `clean` below.
#
# `release`, `debug`, `asan` and `test` are this file's own. The other four are
# what packaging/build_*.sh get by overriding BUILD_DIR on the command line;
# they are listed here so that `make clean` means the same thing whichever
# build left something behind.
OBJECT_DIRS := $(BUILD_DIR)/release $(BUILD_DIR)/debug $(BUILD_DIR)/asan \
               $(BUILD_DIR)/test \
               $(BUILD_DIR)/app $(BUILD_DIR)/windows $(BUILD_DIR)/linux \
               $(BUILD_DIR)/web

# Everything under build/ that is somebody else's library rather than our
# object files: the verified mingw downloads, the SDL checkouts, and the two
# prefixes build_linux.sh and build_web.sh compile SDL, SDL_ttf and SDL_mixer
# into. `sdl3-linux` and `sdl3-web` are each about twenty minutes of cmake and
# are cached per pin — .github/workflows/payloads.yml caches `build/sdl3-linux`
# across runs on exactly that understanding.
SDL_TREES := $(BUILD_DIR)/sdl3-linux $(BUILD_DIR)/sdl3-web \
             $(BUILD_DIR)/SDL-* $(BUILD_DIR)/SDL_ttf-* $(BUILD_DIR)/SDL_mixer-* \
             $(BUILD_DIR)/SDL3-* $(BUILD_DIR)/SDL3_ttf-* $(BUILD_DIR)/SDL3_mixer-*

# Build output only, and only *our* build output.
#
# This was `rm -rf $(BUILD_DIR)`, which also took the SDL trees above with it:
# a `make clean` before a `make web` turned a two-minute rebuild into twenty
# minutes of compiling somebody else's library again, and re-downloaded the
# pinned mingw archives on top. That is the same mistake `dist/` is deliberately
# kept out of this rule to avoid — it holds packaged releases, some of which
# cost a notarization round trip with Apple to make again, and `make clean` is
# what somebody reaches for to force a rebuild rather than to throw those away.
#
# `make sdlclean` is where the libraries live now, and `make distclean` is
# everything.
clean:
	rm -rf $(OBJECT_DIRS) $(TARGET) $(TARGET)_dbg $(TARGET)_asan $(TEST_TARGET) $(EMBEDDED_HEADER)
	@rmdir $(BUILD_DIR) 2>/dev/null || true

# The downloaded and locally built SDL, so the next platform build fetches and
# compiles it again from the pins. vendor/ is the macOS half of the same thing.
sdlclean:
	rm -rf $(SDL_TREES) vendor

# Everything clean removes, the libraries, and the packaged releases with them.
distclean: clean sdlclean
	rm -rf dist

.PHONY: all debug release sanitize run test embed clean sdlclean distclean mac win linux web press sdl3
