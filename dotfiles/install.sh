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
link config/fuzzel/fuzzel.ini .config/fuzzel/fuzzel.ini
link config/swayr/config.toml .config/swayr/config.toml
link config/wob/wob.ini       .config/wob/wob.ini
link config/foot/foot.ini       .config/foot/foot.ini
link config/nwg-dock/style.css  .config/nwg-dock/style.css
link config/gtk-3.0/settings.ini .config/gtk-3.0/settings.ini
link local/applications/chromium.desktop .local/share/applications/chromium.desktop
link home/bashrc              .bashrc
link home/bash_profile        .bash_profile
link home/asoundrc            .asoundrc

echo
echo "Done. Log out and back in, or press Super+Shift+C in sway."
