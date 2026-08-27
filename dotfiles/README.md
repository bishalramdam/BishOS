# dotfiles

The desktop configuration for a BishOS machine: sway, the launcher, the window
switcher, the volume overlay, the shell.

These are **not part of the operating system**. BishOS installs and boots
without any of them. They are what turns a bare sway session into something
usable, and they are here so an evening's tuning is not lost with a USB stick.

## Using them

```sh
./install.sh
```

The files stay here and the usual locations become symlinks pointing back, so
editing `~/.config/sway/config` edits the copy under version control. Nothing
to copy, nothing to forget, no drift.

Anything already in place is moved aside as `.before-dotfiles` rather than
overwritten.

## What is here

| File | What it decides |
| --- | --- |
| `config/sway/config` | The desktop: floating macOS-style windows, keys, startup |
| `config/fuzzel/fuzzel.ini` | The launcher, `Super+Space` |
| `config/swayr/config.toml` | The window switcher, `Super+Tab` |
| `config/wob/wob.ini` | The volume overlay |
| `home/bashrc` | Prompt, history, aliases |
| `home/bash_profile` | Starts the desktop at console login |
| `home/asoundrc` | Which sound card is the default |

## Machine-specific bits

Two things here are true of one machine rather than all of them.

`asoundrc` names card 0 device 3, the HDMI output on that particular monitor.
`config/sway/config` pins PipeWire to the same output, because the monitor
reports `eld_valid 0` -- it claims to have no audio -- and wireplumber would
otherwise switch the card off. It plays perfectly regardless of the claim.

On different hardware, both want changing. Everything else is portable.
