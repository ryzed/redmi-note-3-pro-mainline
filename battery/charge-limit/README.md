# Battery charge limit

The author's kenzo lives on a wall charger (there is no suspend on this kernel), and a lithium battery kept at 100 %
wears out and swells. The charge is held between 70 and 80 %: at 80 % charging is paused, the phone keeps running from
the charger and the battery current is about zero; at 70 % charging resumes. Without a cable charging is left enabled,
so the phone charges as soon as it is plugged in.

Three parts, all ready-made except the thresholds:

- **Kernel** — patch [0029](../../kernel) by zennnez (41bfbcd in
  [zennnez/msm8956-mainline-linux](https://github.com/zennnez/msm8956-mainline-linux)), unchanged:
  `/sys/class/power_supply/qcom-smbchg-usb/charging_enabled` (1 — charge, 0 — pause; the `CHG_EN` bit of
  `BAT_IF_CMD_CHG`). The driver re-enables charging on probe, so every boot starts charging until the daemon runs.
- **Daemon** — `kenzo-batteryd` and its systemd unit by zennnez, used unchanged, from
  [zennnez/KenzoMainlineUbuntu](https://github.com/zennnez/KenzoMainlineUbuntu) (commit 9dad68d):
  `rootfs-overlay/usr/local/sbin/kenzo-batteryd` and `rootfs-overlay/etc/systemd/system/kenzo-batteryd.service`.
  That repository has no license, so the files are not copied here. It is a bash loop that checks every 30 s.
- **Thresholds** — [kenzo-battery.conf](kenzo-battery.conf) here (zennnez's default is 60/58).

The panel indicator [kenzo-battery](..) shows "‖82%" while charging is paused.

## Install (root)

```sh
base=https://raw.githubusercontent.com/zennnez/KenzoMainlineUbuntu/9dad68d2a9e9435fd5ab599d20e7a7eb46d35c87/rootfs-overlay
curl -fsSL -o /usr/local/sbin/kenzo-batteryd "$base/usr/local/sbin/kenzo-batteryd"
curl -fsSL -o /etc/systemd/system/kenzo-batteryd.service "$base/etc/systemd/system/kenzo-batteryd.service"
chmod 755 /usr/local/sbin/kenzo-batteryd
install -m644 kenzo-battery.conf /etc/kenzo-battery.conf
systemctl daemon-reload && systemctl enable --now kenzo-batteryd
```

Read the script before running it as root — it is short.

## Use

- `kenzo-batteryd status` (root) — charge, power source, paused or charging, current.
- Thresholds — edit `/etc/kenzo-battery.conf`; picked up within 30 s.
- Charge to 100 % once: `systemctl stop kenzo-batteryd && kenzo-batteryd enable`; back:
  `systemctl start kenzo-batteryd`.
- A stopped daemon leaves charging as it was: if it was paused, the phone will not charge until
  `kenzo-batteryd enable` or a reboot.

## Checked on the device (2026-11-10)

- At 82 % the daemon paused charging at once; the battery current stayed at about zero (−152 µA) also under load on
  three cores, the voltage did not drop.
- Temporary thresholds 90/85 → charging, +650 mA; back to 80/70 → paused again.
- Not yet: a day on the charger (temperature, the charge staying in 70–80 %).

Separate problem on this phone, not caused by the limit: the charger often goes into "Charger error" with the
battery over-voltage bit set (sometimes 13 s after boot) and stops charging; toggling `charging_enabled` 0 → 1 clears
it. See [../../docs/device-notes.md](../../docs/device-notes.md).
