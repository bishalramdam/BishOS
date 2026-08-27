# matrix-rain

Matrix digital rain as a live wallpaper for sway, drawn by the GPU.

    make
    sudo make install
    matrix-rain

## What it does

A wallpaper on Wayland is not a root window -- X11 had one to draw on and
Wayland has nothing equivalent. It is a surface on the compositor's
*background layer*, requested through the `wlr-layer-shell` protocol, which is
the same mechanism `swaybg` uses. This asks for one per output, anchors it to
all four edges, and gives it an empty input region so clicks pass through to
whatever is behind.

The rain is a single fragment shader. No state is kept between frames and
nothing is simulated on the CPU: each pixel works out from its own coordinates
which column it belongs to, where that column's drop has fallen to at the
current time, and therefore how bright it should be. Column speed, trail
length and starting offset all come from hashing the column index, so the
whole animation is a pure function of `(x, y, time)`.

That is why it is nearly free. On an i5-4590 with HD 4600 it costs roughly 5%
of one core at 30 fps, and the GPU work is a single full-screen pass.

Glyphs are 8x8 bitmaps compiled into the binary (`glyphs.h`), written as text
so they can be edited by eye. There is no font dependency, which matters
because katakana coverage is not something a minimal system has lying around.

## Options

    --fps N        frames per second (default 30)
    --cell N       glyph cell size in pixels (default 16)
    --speed N      fall speed multiplier (default 1.0)
    --density N    share of columns raining, 0..1 (default 0.85)

## Running it from sway

The sway config in `dotfiles/` already does this:

    output * bg #000000 solid_color
    exec_always pkill -x matrix-rain; matrix-rain --cell 12 --density 1.5 --fps 30

`exec_always` rather than `exec` so it survives a config reload, and the
`pkill` first so a reload replaces the old process instead of stacking a
second one on top of it.

The name is short on purpose. Linux truncates a process name to 15
characters, so `matrix-wallpaper` -- 16 -- appears in `/proc` as
`matrix-wallpape` and `pkill -x matrix-wallpaper` silently matches nothing.
`pkill -f` would match instead, but it would also match the shell sway runs
the line in, killing the parent before the wallpaper ever starts. A name that
fits avoids the whole problem.

The background colour stays black rather than an image: if the wallpaper ever
fails to start, a black desktop looks deliberate.

## Building

Needs `wayland-dev`, `wlr-protocols`, `wayland-protocols` and `mesa-dev`.

The layer-shell bindings are generated at build time with `wayland-scanner`,
because layer-shell is a wlroots extension rather than part of Wayland and
ships as XML, not a library. `xdg-shell` is generated too and linked alongside
it -- layer-shell references `xdg_popup` in one request, so the symbol has to
exist even though no popup is ever created here.

## Where it came from

A port of `matrix_rain.py` from `mac-os-npu-projects`, which does the same
thing on an M5 with MLX and prints ANSI to a terminal. The idea is shared --
run the simulation on the GPU, let the display be the cheap part -- but almost
none of the code is, because a terminal and a Wayland layer surface have
nothing in common.
