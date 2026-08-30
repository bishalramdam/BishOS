#!/bin/sh
# Apply the desktop appearance settings GTK actually reads.
#
# ~/.config/gtk-3.0/settings.ini is not enough. GTK3 on Wayland prefers
# GSettings whenever the org.gnome.desktop.interface schema is installed, and
# only falls back to settings.ini when it is not. That schema arrives with
# packages that have nothing obviously to do with theming -- here it came in
# with xdg-desktop-portal-gtk, installed so a Flatpak browser could open a
# file dialog. GTK silently switched source, GSettings said "Adwaita" for
# everything, and the whole theme reverted.
#
# The only visible symptom was two dark icons in Thunar's sidebar: Adwaita's
# user-home is dark and it has no document-open-recent at all, so one icon was
# unreadable and the other missing, while the folder icons happened to look
# similar in both themes and hid the change.
#
# settings.ini is still tracked and still linked. It is the fallback, and it
# records the intent. This script is what makes the intent take effect.
set -e
command -v gsettings >/dev/null || { echo "gsettings not installed; nothing to do"; exit 0; }

I=org.gnome.desktop.interface
gsettings set $I gtk-theme     'WhiteSur-Dark'
gsettings set $I icon-theme    'WhiteSur-dark'
gsettings set $I cursor-theme  'WhiteSur-cursors'
gsettings set $I cursor-size   24
gsettings set $I font-name     'Inter 11'
gsettings set $I color-scheme  'prefer-dark'

echo "applied:"
for k in gtk-theme icon-theme cursor-theme font-name color-scheme; do
    printf '  %-14s %s\n' "$k" "$(gsettings get $I $k)"
done
