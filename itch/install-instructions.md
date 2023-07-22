# Install instructions

Paste this into the page's **Install instructions** field (Edit game → below the
uploads). It is a separate field from the description and it is the one people
read right after downloading, which is the moment a Gatekeeper dialog or a
missing file turns a free game into a shrug.

Keep it short. This is not the place for the features.

---

**Browser** — Press PLAY and it starts. Nothing to install, nothing to unblock.
The bar under the button is the download; most of it is the soundtrack, so give
it a moment on a slow connection. Your settings, unlocked levels and high scores
are kept by the browser, so you can close the tab and come back to them.

**macOS** — Unzip and drag `Breakup.app` wherever you keep your applications. It
is signed and notarized, so it just opens; nothing is installed and nothing is
left behind. Needs macOS 11 or newer, Intel or Apple silicon.

**Windows** — Unzip the folder somewhere and run `breakup.exe`, keeping the
files that came with it in the same folder. Needs 64-bit Windows 10 or newer.
Windows may say the publisher is unknown, because the build is not signed —
_More info_ → _Run anyway_.

**Linux** — Unpack the tarball and run `./breakup`. Keep the `lib/` folder next
to it; that is the copy of SDL the game plays with, and there is nothing to
install. Built on Ubuntu 24.04 for x86_64. On an older distribution, build it
from source — it is one `make`.

**Your save** — Settings, progress and high scores live in one folder:
`~/Library/Application Support/RobertLib/Breakup` on macOS,
`%APPDATA%\RobertLib\Breakup` on Windows,
`~/.local/share/RobertLib/Breakup` on Linux, and the browser's own storage for
the version played on this page. Delete it and the twenty-seven levels start
over from the first. The browser save and a downloaded copy's save are separate.
The game never touches the network.

**Controls** — Mouse or the arrow keys move the paddle, Space (or a click)
serves the ball and fires the lasers, Esc or P pauses, F is fullscreen. They are
not rebindable in this version.
