# The layer-shell protocol is not part of Wayland proper -- it is a wlroots
# extension, so the bindings are generated from the wlroots XML at build time
# rather than linked from a library. wayland-scanner writes both halves: a
# header of stubs and the marshalling code they call.
PREFIX ?= /usr/local
#
# layer-shell also references xdg_popup, so xdg-shell's generated code has to
# be linked alongside it even though no popup is ever created here.
WLR_PROTO ?= /usr/share/wlr-protocols/unstable
WL_PROTO ?= /usr/share/wayland-protocols
LAYER_SHELL_XML := $(WLR_PROTO)/wlr-layer-shell-unstable-v1.xml
XDG_SHELL_XML := $(WL_PROTO)/stable/xdg-shell/xdg-shell.xml

CFLAGS ?= -O2 -Wall -Wextra
LDLIBS := -lwayland-client -lwayland-egl -lEGL -lGLESv2 -lm

GEN := wlr-layer-shell-unstable-v1-client-protocol.h \
       wlr-layer-shell-unstable-v1-protocol.c \
       xdg-shell-client-protocol.h \
       xdg-shell-protocol.c

all: matrix-wallpaper

matrix-wallpaper: matrix-wallpaper.c glyphs.h $(GEN)
	$(CC) $(CFLAGS) -o $@ matrix-wallpaper.c \
	    wlr-layer-shell-unstable-v1-protocol.c xdg-shell-protocol.c $(LDLIBS)

wlr-layer-shell-unstable-v1-client-protocol.h: $(LAYER_SHELL_XML)
	wayland-scanner client-header $< $@

wlr-layer-shell-unstable-v1-protocol.c: $(LAYER_SHELL_XML)
	wayland-scanner private-code $< $@

xdg-shell-client-protocol.h: $(XDG_SHELL_XML)
	wayland-scanner client-header $< $@

xdg-shell-protocol.c: $(XDG_SHELL_XML)
	wayland-scanner private-code $< $@

install: matrix-wallpaper
	install -Dm755 matrix-wallpaper $(DESTDIR)$(PREFIX)/bin/matrix-wallpaper

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/matrix-wallpaper

clean:
	rm -f matrix-wallpaper $(GEN)

.PHONY: all install uninstall clean
