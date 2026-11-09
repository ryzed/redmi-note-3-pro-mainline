# Built-in audio on kenzo with mainline 7.2.3 — what it took

Status (2026-11-10, kernel release 17): loudspeaker, 3.5 mm headphones, phone microphone, PipeWire, automatic
speaker/headphones switching by the headset jack — all work. Not tested: headset microphone and buttons, earpiece,
voice calls (no modem).

## The hardware path

The codec is a WCD9335 ("tasha", v2.0) on SLIMbus. The bus master is the ADSP (Hexagon, msm8976 firmware); Linux
talks to the bus as a satellite through the NGD controller (`qcom-ngd-ctrl`), and the PCM data goes through the ADSP
(QDSP6: ASM → ADM → AFE).

- Playback: MultiMedia1 (`hw:0,0`) → AFE `SLIMBUS_0_RX` → SLIMbus RX channels → WCD9335 `AIF1_PB` (SLIM RX0/RX1) →
  interpolator → LINEOUT4 → external speaker amplifier (enable on GPIO 101), or → HPHL/HPHR for headphones.
- Capture: AMIC1 → ADC1 → DEC0 → SLIM TX0 → `AIF1_CAP` → AFE `SLIMBUS_0_TX` → MultiMedia2 (`hw:0,1`).
- Machine driver: `qcom,apq8096-sndcard` (card name `xiaomi-kenzo`).

The DT and ADSP support came from Magicxavi's branch (patches 0009–0013). With it the ADSP booted, the codec was
detected and every ALSA call succeeded — and the speaker stayed silent.

## Reference: the stock kernel in TWRP

TWRP on kenzo runs the stock LineageOS 3.10 kernel, and its speaker plays. That gave a working reference on the same
phone: with adb as root, the codec registers (debugfs, regmap traces) were dumped while a tone played there and on
mainline. The configuration registers matched; the state did not: `PORT_STATUS0` of RX port 0 read **0x21** during
playback on Android and **0x82** (overflow) on mainline.

## Findings, in the order they mattered

1. **The ADSP will not start a SLIMbus AFE port without active channels.** `AFE_PORT_CMD_DEVICE_START` (0x100e5)
   fails while the SLIMbus channels it names are not active on the bus. So the channels must be up before the AFE
   port starts.

2. **The cause of the silence: an RX port bound to an active channel before its interpolator runs overflows at once
   and never recovers** (`PORT_INT_STATUS` 0x82, `overflow error on RX port 0`). DAPM powers the AIF widget before
   the interpolator, so every variant that enabled the port from `hw_params()` or from the AIF widget event hit it.
   Android (`tasha_codec_enable_slimrx`) programs, connects and activates the RX channel only after the interpolator,
   mix path and DAC are powered; the Fairphone 3 port activates it from the codec `trigger()`. Here the RX stream is
   started from the DAC `PRE_PMU` events (patch 0014): the interpolator already runs, the AFE port is not started yet.
   The port then reads 0x21 while playing, as on Android.

3. **Stop order: AFE first, then the channels.** Removing the SLIMbus channels under a running AFE port crashed the
   ADSP and rebooted the phone. The AFE port is now stopped in the CPU DAI's `hw_free()`, which runs before the
   codec's, and the codec removes the channels after it (0014, 0016). The RX port registers stay programmed on stop,
   as on Android — clearing them first left the port bound and made the next start overflow.

4. **A deadlock on the APR receive path.** Waiting for an AFE reply from `trigger()` (PCM stream lock held) timed
   out with -110 as soon as an ASM event was queued in front of the reply: the ASM event handler calls
   `snd_pcm_period_elapsed()`, which needs the same lock, and both arrive on one APR receive path. A small buffer made
   it happen on the very first start. The AFE port start is now a work item queued from `trigger()` (0016).

5. **Restarting a port without a reboot** failed with `0x100e5 error 1` until the NGD controller learned
   `disable_stream` (upstream commit 099460e, patch 0017).

6. **Capture recorded digital zeros unless something was playing.** MCLK was routed from `RX_BIAS` only; the mic bias
   supplies now take MCLK too, as on apq8096-db820c (0018). A tone from another device recorded at 1500 Hz came back
   as 1500.04 Hz, so the codec clock is right.

7. **24-bit playback was ~48 dB too quiet.** The ASM media format `MULTI_CHANNEL_PCM_V2` expects 24 significant bits
   at the top of a 32-bit word; ALSA `S24_LE` keeps them in the low three bytes, so the DSP read every sample 256×
   too small (measured −38.7 dB through the speaker compander, −48 dB expected without it). PipeWire picks
   `s24-32le` by itself. Patch 0019 stops offering S24_LE; offering S32_LE with `msbits=24` instead would keep real
   24-bit output and was left as an option.

8. **Headset jack (MBHC).** Mainline WCD9335 had no jack detection; mainline `wcd-mbhc-v2` only knows the MBHC ADC,
   which this codec does not have. The Fairphone 3 series (0020–0024: a legacy comparator backend for `wcd-mbhc-v2`
   and wcd9335 on the shared MBHC) applied unchanged. kenzo specifics: both jack switches are normally closed (stock
   DT: `qcom,msm-mbhc-hphl-swh` and `qcom,msm-mbhc-gnd-swh` = 0) and the headset comparator reference is 1.7 V, as in
   the stock calibration (0025–0027). Measured: one interrupt per plug movement, no spurious key events; for ~3 s
   after an insertion the button interrupt fires repeatedly while the legacy code checks for a microphone.

9. **An empty SLIMbus stream took the whole card down.** At startup PipeWire opens the capture PCM before UCM
   connects the microphone path in the codec mixer. Capture `hw_params()` enabled the SLIMbus stream with zero
   channels, the ADSP never answered that channel definition (`TX timed out:MC:0x21`, -110 every second), and
   PipeWire left the card without profiles — no playback either. Patch 0028 skips a stream without channels.

10. **Userspace: profiles, not ports.** Switching the codec path under an open stream overflows the RX port (2),
    so speaker and headphones are two card profiles and the jack switches profiles ([../audio](../audio)).

## Methods that helped

- Test tones at a different frequency every run, recorded by a microphone next to the phone and measured
  (`audio/tools/pitch.mts`): the frequency and level answer "did it play" without relying on ears alone.
- Register dumps and ftrace of regmap writes on both the stock kernel (TWRP) and mainline, compared side by side.
- Building one module in a persistent kernel tree and swapping it on the phone instead of reflashing the kernel.
