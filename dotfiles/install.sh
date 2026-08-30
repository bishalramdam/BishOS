#!/bin/sh
# Link these configs into place.
#
# The files live here, in the repository, and the locations programs expect
# are symlinks pointing back. So editing ~/.config/sway/config edits the file
# under version control -- there is no copying step to forget, and nothing can
# drift out of sync.
#
# Run this once on a new install, after cloning the repository.

set -e
DOT="$(cd "$(dirname "$0")" && pwd)"

link() {
    src="$DOT/$1"
    dst="$HOME/$2"
    [ -e "$src" ] || { echo "missing in repo: $1"; return; }
    mkdir -p "$(dirname "$dst")"
    # Move anything already there aside rather than destroying it.
    if [ -e "$dst" ] && [ ! -L "$dst" ]; then
        mv "$dst" "$dst.before-dotfiles"
        echo "kept your old $2 as $2.before-dotfiles"
    fi
    ln -sfn "$src" "$dst"
    echo "linked $2"
}

link config/sway/config       .config/sway/config
# labwc, the second compositor. ~/.use-labwc chooses it at login.
link config/labwc/rc.xml         .config/labwc/rc.xml
link config/labwc/autostart      .config/labwc/autostart
link config/labwc/environment    .config/labwc/environment
link config/fuzzel/fuzzel.ini .config/fuzzel/fuzzel.ini
link config/swayr/config.toml .config/swayr/config.toml
link config/wob/wob.ini       .config/wob/wob.ini
link config/foot/foot.ini       .config/foot/foot.ini
link config/nwg-dock/style.css  .config/nwg-dock/style.css
link config/gtk-3.0/settings.ini .config/gtk-3.0/settings.ini
# GTK4 reads its own file and ignores the GTK3 one, so loupe and other GTK4
# programs keep the default look unless this is linked too.
link config/gtk-4.0/settings.ini .config/gtk-4.0/settings.ini
# Renames Thunar to Finder in the launcher. A file here wins over the one in
# /usr/share, so a package update will not overwrite it.
link local/applications/thunar.desktop .local/share/applications/thunar.desktop
# The packaged typobuster.desktop has no %f, so opening a file with it opened
# nothing. This one passes the path and declares the MIME types.
link local/applications/typobuster.desktop .local/share/applications/typobuster.desktop
link local/applications/chromium.desktop .local/share/applications/chromium.desktop
link home/bashrc              .bashrc
link home/bash_profile        .bash_profile
link home/asoundrc            .asoundrc

echo
echo "Done. Log out and back in, or press Super+Shift+C in sway."
