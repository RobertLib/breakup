# Third-party material

Everything in this repository is covered by [LICENSE](LICENSE) (MIT) **except the
files listed below**, which somebody else wrote and which this project only
redistributes. Every sprite, background and sound effect is still generated in
code at runtime; the font and the nineteen music tracks are the assets on disk,
and they are the things here that are not ours to license.

## assets/music/*.ogg — Three Red Hearts, by Abstraction

- **Author:** Benjamin Burnes, as **Abstraction** —
  <https://abstractionmusic.com/> / Tallbeard Studios
- **Where it came from:** the _Music Loop Bundle_ asset pack,
  <https://tallbeard.itch.io/music-loop-bundle> — the loop-ready
  Ogg Vorbis cut of the 2014 chiptune album _Three Red Hearts_,
  <https://abstractionmusic.bandcamp.com/album/three-red-hearts>.
- **Licence: CC0 1.0 Universal (public domain dedication)** —
  <https://creativecommons.org/publicdomain/zero/1.0/>. The pack's own
  licence file, kept here as `assets/music/LICENSE.txt`, says it outright: _"To the extent possible under law,
  Abstraction Music and Tallbeard Studios has waived all copyright and related
  or neighboring rights to the music contained in this asset pack. All assets
  are available to use in any commercial or non-commercial project, and may be
  modified in any way the user chooses."_ Nothing is owed and nothing is asked —
  which is the whole reason these are the tracks that are here rather than some
  other ones. Credit is given below because it should be, not because it must.
- **How this project uses it:** nineteen of the album's tracks — every distinct
  song the pack contains — embedded into the binary by `embed_assets.sh` and
  decoded by SDL_mixer while they play. They are **re-encoded rather than
  byte-for-byte as published**: Vorbis quality 4, about 128 kbps, down from the
  250–280 kbps the pack ships at, which takes the soundtrack from 40 MB to 18
  MB. The licence allows it outright — the pack's own terms say the assets "may
  be modified in any way the user chooses" — and the reason is the browser
  build, where the full-quality soundtrack was 40 MB of a 44 MB download and a
  player who closes the loading bar hears none of it. Each re-encode decodes to
  exactly as many samples as the original, so the loops are still seamless, and
  each carries the artist, title and album comments the pack set. Two songs ship
  as a pair of mixes and the pair is picked between rather than used twice:
  _Princess Quest_ is taken without its "boing", which is a sound effect and
  would have argued with the game's own, and _Go_ is taken with its vocal,
  because the instrumental mix drops three decibels along with it and would have
  been the one track in the game that sounds like the volume slipped. The pack's
  own licence file is kept beside them as
  [`assets/music/LICENSE.txt`](assets/music/LICENSE.txt), so the terms travel
  with the asset rather than living only here — and, since `embed_assets.sh`
  takes it too, they travel inside the shipped binary as well.
- **What the author asks, and this project does anyway:** credit as
  "Abstraction", with a link to the pack or to abstractionmusic.com. He also
  asks — outside the licence, as a preference — that the music not be used in
  NFT, AI/machine-learning or asset-resale projects. This game is none of those.

| File                        | Track                     |
| --------------------------- | ------------------------- |
| `assets/music/menu.ogg`     | Modern Bits               |
| `assets/music/world1-a.ogg` | Princess Quest (No Boing) |
| `assets/music/world1-b.ogg` | Puzzle Pieces             |
| `assets/music/world1-c.ogg` | Candy                     |
| `assets/music/world1-d.ogg` | Penguin Town              |
| `assets/music/world2-a.ogg` | Box Jump                  |
| `assets/music/world2-b.ogg` | Deep Blue                 |
| `assets/music/world2-c.ogg` | Connected                 |
| `assets/music/world2-d.ogg` | Rabbit Town               |
| `assets/music/world3-a.ogg` | Pixel War 1               |
| `assets/music/world3-b.ogg` | Pixel War 2               |
| `assets/music/world3-c.ogg` | Penguins vs Rabbits       |
| `assets/music/world3-d.ogg` | Go                        |
| `assets/music/world4-a.ogg` | Sanctuary                 |
| `assets/music/world4-b.ogg` | Out of Time               |
| `assets/music/world4-c.ogg` | Penultimate               |
| `assets/music/world4-d.ogg` | Three Red Hearts          |
| `assets/music/boss-a.ogg`   | Rumble at the Gates       |
| `assets/music/boss-b.ogg`   | Save the City             |

## assets/font.ttf — PC Senior

- **Author:** codeman38 (Zone38) — <https://www.zone38.net/font/>
- **What it is:** a TrueType conversion of the 8×8 pixel font from IBM CGA
  hardware, released in 2004. The name table in the file carries
  "TrueType conversion © 2001 codeman38".
- **How this project uses it:** embedded verbatim into the binary by
  `embed_assets.sh`, unmodified, and rendered by SDL_ttf.

### Licence status — read this before shipping

The author's own page states an explicit licence for exactly one of his fonts
(Press Start 2P, under the SIL Open Font License) and **states none for PC
Senior**. The terms circulated by the font distributors — that it is free for
personal and commercial use, may be bundled in free or commercial projects, may
not be re-sold as part of a commercial font collection, and asks for credit to
codeman38 / zone38.net on modified versions — come from those distributors
rather than from a licence file in this repository.

Checked again on 2026-09-19: <https://www.zone38.net/font/> still names the SIL
Open Font License for Press Start 2P and still says nothing at all about PC
Senior, and the PC Senior downloads on that page are not documented as carrying
a readme or a licence file. So option 1 below may not be available, and this is
still open.

That is second-hand, and a released game is the wrong place to find out it was
wrong. Before a public release, do one of:

1. Get the original download from zone38.net and keep its readme/licence file
   next to the font in this repository, so the terms travel with the asset; or
2. Ask codeman38 directly and record the answer here; or
3. Switch to a font with an unambiguous licence — **Press Start 2P** is by the
   same author, is the same kind of pixel face, and is OFL. That is a change to
   how the whole game looks, so it is a design decision and not a formality.

Credit is given regardless of which of those it turns out to be: it costs
nothing and the font deserves it.
