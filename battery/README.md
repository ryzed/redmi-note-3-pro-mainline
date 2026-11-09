# kenzo-battery

Battery charge as text in the jwm panel: "82%", while charging "⚡82%", in red
below 15 %. The data comes from `/sys/class/power_supply/qcom-battery`
(`capacity`, `status`), every 30 s. The font and colors are the same as the
panel clock (`DejaVu Sans` 27 px, `#FFFFFF` on `#202830`).

```
apk add gcc musl-dev libx11-dev libxft-dev make   # build dependencies
make && sudo make install                         # → /usr/local/bin/kenzo-battery
```

In the `<Tray>` of `~/.jwmrc`, before `<Clock>`:

```
<Swallow name="kenzo-battery" width="110">kenzo-battery</Swallow>
```

Why it is done this way (2026-11-09):

- The ready-made options did not fit: the `cbatticon` icon (GTK3) was not drawn
  in the jwm tray; `conky` draws with double buffering (DBE), and with
  modesetting + glamor its redraws did not reach the screen — the `scrot`
  screenshot showed the text, but the phone screen was empty (it appeared only
  when the screen was rotated); without DBE, `conky` 1.25 with Xft crashed.
- The label is drawn into a pixmap, which becomes the window background — X
  itself puts it on the screen.
- `kenzo-osk-toggle` restarts jwm (`jwm -restart`); jwm releases the swallowed
  window and runs the `<Swallow>` command again. An instance that was returned
  to the root window exits on its own — otherwise it would remain an ordinary
  window at the bottom of the screen.
- The window is shown only when the window manager has announced itself
  (`_NET_SUPPORTING_WM_CHECK`), plus 0.5 s: at session start, jwm does not pick
  up a window that was shown earlier into the panel.
