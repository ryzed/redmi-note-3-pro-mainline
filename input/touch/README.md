# kenzo-touch

The Redmi Note 3 touchscreen as an ordinary mouse: tap, drag, hold for a
right click, two fingers to scroll (as on a phone: the content moves with the
fingers, since 2026-11-09).

It is needed because libinput provides neither right click nor scrolling on a
touchscreen (this is a touchscreen, not a touchpad), and the evdev driver with
third-button emulation does not work at all on a multitouch screen — checked
2026-11-04.

The daemon takes the touchscreen for itself through `libevdev` (`EVIOCGRAB`),
so X does not see the touches and there is no extra left click. It sends mouse
events through XTest — the same way svkbd sends keys.

## Gestures

| gesture | what it does |
|---|---|
| tap | left click |
| finger slide | drag — the button is pressed at the touch point |
| hold ≈0.6 s | right click |
| two fingers up/down/left/right | mouse wheel |
| tap on a blanked screen | only wakes the screen, no click |
| touch on the on-screen keyboard | left button held down while the finger is on the screen |

The first touch on a blanked screen does not count as a click: otherwise the
tap would also press a button in the window under the finger. The display
panel state is read from sysfs (`/sys/class/drm/card0-DSI-1/dpms`), not from X:
`xset q` keeps saying "Monitor is Off" even after the screen has woken up.

There are no gestures on svkbd (a window of class `svkbd`). svkbd handles long
presses itself — ё and ъ, the choice of è é ê, Backspace repeat — and for that
the left button must be held down. svkbd interprets a right click on a key as
Alt+key (`buttonmods` in the layout), so a hold with gestures enabled would
type `ö` instead of `v`.

The thresholds are constants at the top of `kenzo-touch.c`, in screen pixels:
`HOLD_MS`, `MOVE_PX` (20 px), `SCROLL_STEP` (75 px per wheel click). The
touchscreen reports 0..1023 on both axes for a 1080×1920 screen, so raw units
are no good for thresholds.

A click (tap, right click) is a press, a pause of `CLICK_MS` (60 ms), then a
release.

Warning: without the pause, the jwm panel buttons ("keyboard", "rotate") do not
fire: on a press jwm grabs the mouse and waits for the release, and anything
that arrived before the grab is lost (2026-11-06: without the pause — 0 of 2,
with a pause of 10 ms or more — 6 of 6). The touchpad of a BT keyboard does not
show this — a real finger always holds longer. The wheel has no pause.

After the kernel drops events (`SYN_DROPPED`), libevdev delivers the current
state of the fingers in one batch in which every event comes with the code
`LIBEVDEV_READ_STATUS_SYNC` (not `SUCCESS`), and the end is `-EAGAIN`. A loop
keyed on `SUCCESS` exited immediately, and the first tap after the loss was
dropped.

## Screen rotation

The daemon takes the screen size and rotation from X (RandR): it reads them at
start and is subscribed to `RRScreenChangeNotify`. The "⟳" button in the jwm
panel (`session/kenzo-rotate`) switches between portrait and landscape, while
the touchscreen axes stay physical, so the point is computed from both axes at
once (`map_point`): for `xrandr --rotate right`, logical x follows raw y, and
logical y follows raw x, inverted. The other positions (`left`, `inverted`) are
worked out the same way. A rotation in the middle of a gesture resets the
gesture: the left button is released, then the daemon waits for the fingers to
lift. The thresholds stay in screen pixels. The dependency is `libxrandr-dev`,
`-lXrandr` in the `Makefile`. In the log (`-v`) there is a line
`экран 1920x1080, поворот 270` ("screen 1920x1080, rotation 270") — at start
and after every change.

## Files

| file | where it goes on the phone |
|---|---|
| `kenzo-touch.c`, `Makefile` | build; the binary goes to `/usr/local/bin/kenzo-touch` |
| `99-kenzo-touch.rules` | `/etc/udev/rules.d/` |
| `50-touch-ignore.conf` | `/etc/X11/xorg.conf.d/` |
| `kenzo-touch.service` | `~/.config/systemd/user/` |
| `test/` | test on a virtual touchscreen, see below |

## Building on the phone

```
apk add gcc musl-dev libevdev-dev libxtst-dev libx11-dev libxext-dev libxrandr-dev make
make
sudo install -m 755 kenzo-touch /usr/local/bin/
```

## Device access

The touchscreen belongs to `root:input`, the session user is not in the `input`
group, and `uaccess` does not work in this udev build (no ACL appears). The
rule `99-kenzo-touch.rules` gives the device to the `plugdev` group and gives
it the persistent name `/dev/input/kenzo-touch`, which is what the daemon
opens. The match is by device name, not by `eventN`: the number depends on the
order in which the kernel found the devices and can shift. The daemon checks
that it opened the touchscreen and does not grab any other device.

```
sudo install -m 644 99-kenzo-touch.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --action=change /sys/class/input/event3
ls -l /dev/input/kenzo-touch
```

## Running

A user unit, enabled for autostart:

```
install -m 644 kenzo-touch.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now kenzo-touch
```

The daemon keeps a connection to X: it starts before X and crashes until X is
up, and it also crashes when X is restarted. `Restart=always` brings it back,
and `StartLimitIntervalSec=0` removes the systemd limit (5 starts in 10 s) —
otherwise, with a slow X start, the daemon would stay down and the screen would
have no touch. The gesture log is `journalctl --user -u kenzo-touch` (flag
`-v`).

## X configuration

The touchscreen is excluded from libinput (`50-touch-ignore.conf`); otherwise X
would get the same touches and produce a second, extra left click.

## Testing

`test/` holds a virtual touchscreen (`vtouch`, uinput) and scenarios. A test
copy of the daemon works on the virtual device next to the real one, and the
touches arrive in the real X. Before a run: a maximized xterm in focus and
svkbd on the `en` layer; take a screenshot and know what is under the finger.

```
make -C test
test/run.sh test/keyboard.txt [./kenzo-touch]
test/run.sh test/gestures.txt [./kenzo-touch]
ROTATE=right test/run.sh test/landscape.txt [./kenzo-touch]
```

`ROTATE=right` rotates the screen before the run and rotates it back after; for
the duration of the run an automatic revert to portrait is set (timer
`kenzo-rotate-revert`, 4 min). The `landscape.txt` scenario is for landscape
(`right`).

What the result should be is described in the comments at the top of each
scenario.

## Rollback

```
systemctl --user disable --now kenzo-touch
sudo rm /etc/X11/xorg.conf.d/50-touch-ignore.conf /etc/udev/rules.d/99-kenzo-touch.rules
sudo systemctl restart getty@tty1
```

Touch goes through libinput again, as on an ordinary touchscreen.
