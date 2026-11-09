# Device notes

Xiaomi Redmi Note 3 Pro "kenzo" (MSM8956, 2 GB RAM, 16 GB eMMC, Atmel maXTouch touchscreen) on postmarketOS/Nura
edge, port `qcom-msm8956` (testing). Bootloader lk2nd in the `boot` partition, the system in `userdata`, TWRP in
`recovery` as the rescue path.

## Works

- Display through DRM: Xorg `modesetting` at 1080×1920, Freedreno FD510 (GLES 3.1). The hardware cursor is invisible
  on mdp5 — `Option "SWcursor" "true"` in `/etc/X11/xorg.conf.d/10-modesetting.conf`.
- Touchscreen (with kernel patch 0004 — the controller NAKs the first I2C transfer after sleep and used to hang with
  "Failed to read T44 and T5"). libinput gives no right click and no scrolling on a touchscreen, hence
  [../input/touch](../input/touch).
- Wi-Fi (`wcn36xx`), Bluetooth (headphones over PipeWire work).
- Battery and charging (`/sys/class/power_supply/qcom-battery`); the charger limits a wall charger to 1 A, a computer
  USB port gives 500 mA and the phone discharges under load.
- Built-in audio — kernel release 17, see [audio.md](audio.md).
- Both Cortex-A72 — under observation, see [a72.md](a72.md).
- 12 temperature sensors, the SD card slot.

## Does not work / not in the port

Hardware video decoding ([venus.md](venus.md)), camera, modem, motion and light sensors, notification LED, vibration.

## Gotchas

- **No suspend.** `/sys/power/state` is empty; "screen off" can only be DPMS. Writing `bl_power=4` to sysfs turns the
  backlight off, but input does not wake it — X does not know the screen is off. Read the panel state from
  `/sys/class/drm/card0-DSI-1/dpms`; `xset q` keeps saying "Monitor is Off" after wake-up.
- **Power key.** `postmarketos-base-systemd` sets `HandlePowerKey=ignore`; a user cannot power off without polkit
  answering `challenge` (no agent in a bare X session). See [../input/power](../input/power).
- **RTC** (`rtc-pm8xxx`) sits at 1970 and is read-only (`rtc@6000` has no `allow-set-time`); until NTP the time comes
  from the last shutdown (`/var/lib/systemd/timesync/clock`).
- **IPv6.** If the router announces an IPv6 route without an address, programs stumble over AAAA records. The author
  disables IPv6 in the Wi-Fi profile and pins the 5 GHz band (connects in ~24 s after boot instead of ~214 s).
- **Wi-Fi drops** under `wcn36xx` (ping 7–110 ms, "Bad TX complete indication" in the log). Long jobs over ssh should
  run as a service (`systemd-run --unit=… --wait`): logind kills a plain `nohup … &` when the session ends.
- **Flashing.** `fastboot` from lk2nd cannot flash a `userdata` image bigger than its download buffer (511 MB). Flash
  through TWRP instead: `adb exec-in 'dd of=/dev/block/mmcblk0p46 bs=1048576' < image`, then compare checksums.
- **lk2nd fastboot:** press volume down right after the vibration.
- **Kernel updates.** The custom kernel is installed from a file with `apk add --allow-untrusted`, which pins it in
  `/etc/apk/world`; `apk upgrade` leaves it alone.
