# Audio userspace: UCM, WirePlumber, package

Needs the kernel from [../kernel](../kernel) (release 17 or later). What works: loudspeaker, 3.5 mm headphones,
the phone microphone, playback and capture through PipeWire, software volume and mute, and **automatic switching
between speaker and headphones when a plug goes in or out** (also while playing). Not tested: headsets with a
microphone and buttons, the earpiece, booting with headphones already plugged in. Why the kernel side looks the
way it does — [../docs/audio.md](../docs/audio.md).

## Files

| file | installed to | what |
|---|---|---|
| `ucm/Xiaomi/kenzo/kenzo.conf`, `HiFi.conf` | `/usr/share/alsa/ucm2/Xiaomi/kenzo/` + link `conf.d/apq8096/xiaomi-kenzo.conf` | ALSA UCM for the card `xiaomi-kenzo` (driver `snd_soc_apq8096`): verb HiFi, devices Speaker, Headphones (jack `Headset Jack`), Mic |
| `wireplumber/51-kenzo-alsa.conf` | `/usr/share/wireplumber/wireplumber.conf.d/` | software volume for the card (`api.alsa.soft-mixer`), loads the hook below |
| `wireplumber/kenzo-jack-profile.lua` | `/usr/share/wireplumber/scripts/` | the headset jack picks the card profile |
| `package/APKBUILD` | — | Alpine package `kenzo-audio-config` with all of the above |
| `tools/snd-play.sh` | — | a test tone straight through ALSA (`hw:0,0`), speaker or headphones; stop PipeWire first (`systemctl --user stop pipewire.socket pipewire-pulse.socket pipewire pipewire-pulse wireplumber`) |
| `tools/jack-watch.sh` | — | logs the jack control and the MBHC interrupt counters while you plug and unplug |
| `tools/pitch.mts` | — | Node script: test-tone WAV files, and the frequency/level of a tone in a recording |

Code comments in these files are in Russian.

## How it fits together

- **Two profiles, not two ports.** Speaker and Headphones are `ConflictingDevice`s in UCM, so PipeWire shows two card
  profiles: `HiFi (Mic, Speaker)` and `HiFi (Headphones, Mic)`. A profile switch closes and reopens the PCM. Switching
  the codec path under an open stream (what a port switch inside one profile would do) overflows the SLIMbus RX
  port and the output stays silent.
- **The jack.** The kernel creates the jack in the codec driver: an input device `xiaomi-kenzo Headset Jack`
  (`SW_HEADPHONE_INSERT`, `SW_MICROPHONE_INSERT`, `SW_JACK_PHYSICAL_INSERT`) and the ALSA control
  `iface=CARD,name='Headset Jack'` (`amixer -c0 cget iface=CARD,name='Headset Jack'`). UCM names it as the
  `JackControl` of Headphones, so PipeWire marks the headphones available or not.
- **The hook.** Stock WirePlumber 0.5 follows a newly available port only within the current profile and keeps the
  current profile while it is available. `kenzo-jack-profile.lua` decides for this card: headphones available → the
  Headphones profile, not available → the Speaker profile, re-evaluated whenever the plug moves. A manual choice
  (`pavucontrol`, `pactl set-card-profile`, `session/set-output`) holds until the plug moves again.
- **Format.** With kernel release 17 the driver offers only S16_LE; PipeWire picks it by itself. (Older kernels
  offered S24_LE, which played ~48 dB too quiet — see docs/audio.md.)
- **Volume.** The codec gains are fixed in UCM; volume and mute are PipeWire software volume. Without
  `api.alsa.soft-mixer` PipeWire turned a codec control that is not on the speaker path, and the system slider did
  nothing.

## Install

Build the package with pmbootstrap: put `package/APKBUILD` and the four source files (`kenzo.conf`, `HiFi.conf`,
`51-kenzo-alsa.conf`, `kenzo-jack-profile.lua`) into `pmaports/device/testing/kenzo-audio-config/`, then
`pmbootstrap checksum kenzo-audio-config && pmbootstrap build kenzo-audio-config` (a noarch package — it lands under
`packages/edge/<build arch>/`). On the phone: `apk add --allow-untrusted kenzo-audio-config-1-r2.apk`, then restart
the session's PipeWire: `systemctl --user restart pipewire pipewire-pulse wireplumber`.

Or copy the files by hand to the paths in the table.

Check: `pactl list cards` shows both profiles; with headphones out the Headphones port is "not available".

## Volume when testing

The tone files are at -10 dBFS. With the headphones on the analog maximum, keep the PipeWire volume low (the
author's headphone route is stored at 30 %).
