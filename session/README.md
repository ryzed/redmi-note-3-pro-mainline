# X session on kenzo

Session scripts and jwm panel icons. Touch and the power button are separate
daemons in `../input/touch` and `../input/power`.

## Files

| file | where it goes on the phone | what it does |
|---|---|---|
| `kenzo-session` | `/usr/local/bin/` | the session started from `~/.xinitrc`: keyboard, then `exec jwm` |
| `kenzo-osk-toggle` | `/usr/local/bin/` | show/hide svkbd; `on` — only show |
| `kenzo-rotate` | `/usr/local/bin/` | portrait ↔ landscape, the "rotate" button in the panel |
| `icons/keyboard.svg`, `icons/rotate.svg` | `/usr/local/share/kenzo/` | icons for the panel buttons |
| `set-volume` | `/usr/local/bin/` | sound volume: `set-volume 50`, `+5`, `-5`, no argument — show (not above 100 %) |
| `mute` | `/usr/local/bin/` | "mute": `mute` toggles, `mute on`, `mute off` |
| `set-output` | `/usr/local/bin/` | speaker or wired headphones: `set-output speaker`, `headphones` (PipeWire card profile); since kernel r17 the output follows the jack by itself, the command is a manual choice until the plug next moves |
| `rotate` | `/usr/local/bin/` | a short name for `kenzo-rotate`, for the terminal |
| `set-brightness` | `/usr/local/bin/` | screen brightness: `set-brightness 30`, `+10`, `-10`, no argument — show (not below 2 %) |

```
sudo install -m 755 kenzo-session kenzo-osk-toggle kenzo-rotate set-volume mute set-output rotate set-brightness /usr/local/bin/
sudo install -d /usr/local/share/kenzo
sudo install -m 644 icons/*.svg /usr/local/share/kenzo/
```

## jwm panel

Buttons in the `<Tray>` of `~/.jwmrc`:

```
<TrayButton icon="/usr/local/share/kenzo/keyboard.svg" popup="Keyboard">exec:kenzo-osk-toggle</TrayButton>
<TrayButton icon="/usr/local/share/kenzo/rotate.svg" popup="Rotate screen">exec:kenzo-rotate</TrayButton>
```

The battery indicator is a program of its own, `kenzo-battery` (`../battery`,
since 2026-11-09): "82%" as text between the buttons and the clock,
`<Swallow name="kenzo-battery" width="110">kenzo-battery</Swallow>` before
`<Clock>`. The ready-made `cbatticon` and `conky` did not fit — why, in
`../battery/README.md`.
Brightness is writable by the `video` group
(`/sys/class/backlight/backlight/brightness`); the user has been in it since
2026-11-09.

Ctrl exists on the svkbd-mobile-intl on-screen keyboard on the English layer —
the "^" key at the start of the third row, left of "a" (`XK_Control_L`); on the
symbols layer — "Ctrl" in the bottom row. The modifier is sticky: for Ctrl+C
touch "^", then "c".

Icons instead of the characters "⌨" and "⟳": jwm stretches an icon to the full
height of the panel (96 px), so the button comes out about 96×96 instead of
about 35×96 — a finger cannot hit a character. jwm is built with SVG support.
To check the config — `jwm -p`, to apply it — `jwm -restart`.

## Screen rotation (`kenzo-rotate`)

Only `xrandr --output DSI-1 --rotate right` (clockwise: the top of the picture
is by the power button on the right edge) and back to `normal`. The position is
not stored across reboots — X always starts in portrait.

- **Touch** sees the rotation by itself: `kenzo-touch` listens to RandR.
- **svkbd** does not change its width for the new screen, so the script closes
  a shown keyboard before the rotation and brings it up again after:
  `kenzo-osk-toggle` will stretch it to the screen width (in landscape
  1920×385, in portrait 1080×685), press it to the bottom and declare the strip
  occupied. The layer (en/ru) is reset to en. A hidden keyboard stays hidden.
- **jwm** rearranges the panel and maximized windows by itself; the script
  checks `_NET_WORKAREA` and, if it does not match, runs `jwm -restart`.
- **mpv** rotates horizontal video by 90° only in portrait — the condition of
  the `[landscape]` profile in `~/.config/mpv/mpv.conf` looks at
  `display_width > display_height`. In landscape the video judders more (the X
  rotation is an extra frame copy); watching is more comfortable in portrait.

Pitfalls:
- Warning: svkbd inherits open descriptors: the `flock` lock (descriptor 9) is
  closed when the keyboard is started (`kenzo-osk-toggle on 9>&-`), otherwise
  the keyboard holds the lock for as long as it lives, and the next button
  press goes nowhere.
- Warning: outside the panel (over ssh), run `kenzo-rotate` as a separate
  service: `systemd-run --user --wait --collect -p KillMode=none -E DISPLAY=:0
  /usr/local/bin/kenzo-rotate`. An svkbd started from an ssh session or an
  ordinary service dies together with it.
