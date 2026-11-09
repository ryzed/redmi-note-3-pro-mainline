# kenzo-power

The Redmi Note 3 power button as on a phone: a short press blanks and wakes
the screen, a hold opens the jwm menu.

There is no sleep mode on this kernel (`/sys/power/state` is empty), so
"blanking the screen" means DPMS: the display panel and backlight are switched
off, while the system, Wi-Fi and ssh stay up. Any input turns the screen back
on.

The daemon takes `pm8941_pwrkey` for itself through `libevdev` (`EVIOCGRAB`),
so X does not receive `XF86PowerOff` and logind does not receive `KEY_POWER`
(in `postmarketos-base-systemd` it is `HandlePowerKey=ignore` anyway).

## What the button does

| action | screen on | screen off |
|---|---|---|
| short press | turns off | turns on |
| hold ≈0.8 s | jwm menu | turns on |

The decision is made when the button is released — only then is it clear
whether the press was short or long. The threshold is the `HOLD_MS` constant at
the top of `kenzo-power.c`.

The menu is the jwm root menu (the same one that opens on `Super_L`). The
"Power off" and "Reboot" items live in `~/.jwmrc` and call
`sudo -n systemctl …`: shutdown through polkit as the user does not work (it
answers `challenge`, there is no agent in the session).

## Files

| file | where it goes on the phone |
|---|---|
| `kenzo-power.c`, `Makefile` | build; the binary goes to `/usr/local/bin/kenzo-power` |
| `99-kenzo-power.rules` | `/etc/udev/rules.d/` |
| `kenzo-power.service` | `~/.config/systemd/user/` |

## Building on the phone

```
apk add gcc musl-dev libevdev-dev libx11-dev libxext-dev libxtst-dev make
make
sudo install -m 755 kenzo-power /usr/local/bin/
```

## Device access

The button belongs to `root:input`, the session user is not in the `input`
group, and `uaccess` does not work in this udev build. The rule
`99-kenzo-power.rules` gives it to the `plugdev` group and gives it the
persistent name `/dev/input/kenzo-pwrkey`, which is what the daemon opens. The
match is by device name, not by `eventN`: the number can shift. The daemon
checks that the device has `KEY_POWER` and does not grab any other device.

```
sudo install -m 644 99-kenzo-power.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --action=change /sys/class/input/event0
ls -l /dev/input/kenzo-pwrkey
```

## Running

The user unit `kenzo-power.service`, enabled for autostart:

```
install -m 644 kenzo-power.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now kenzo-power
```

The daemon keeps a connection to X: it starts before X and crashes until X is
up, and it also crashes when X is restarted. `Restart=always` brings it back,
and `StartLimitIntervalSec=0` removes the systemd limit (5 starts in 10 s). The
log is `journalctl --user -u kenzo-power`; with the `-v` flag the daemon writes
every recognized action.

## Rollback

```
systemctl --user disable --now kenzo-power
sudo rm /etc/udev/rules.d/99-kenzo-power.rules
```

The button goes back to X and logind, and they do nothing with it — it simply
stops working.
