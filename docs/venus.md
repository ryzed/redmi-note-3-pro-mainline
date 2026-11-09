# Venus (hardware video decoding) — does not work yet

Patches 0001–0003 and 0005 add Venus for MSM8956. State on 2026-11-04, unchanged since:

- Firmware `venus-v1` (VIDEO.VE.4.0-00096), copied from the Android system partition (`/etc/firmware`, MIUI 5.1.1)
  to `/lib/firmware/qcom/msm8956/`. Node parameters are from the stock kenzo device tree.
- Hardware 3.32.1; the firmware loads and the 3xx protocol answers: start, codec list, power collapse before the
  first session — clean. `/dev/video0` is the encoder, `/dev/video1` the decoder, mpv offers `v4l2m2m-copy`.
- With fresh firmware the first session opens and the bitstream is accepted, but no frames come out: the
  format-change event never arrives (ffmpeg asks `G_FMT` about 18 000 times and gives up). After that power collapse
  fails with "wait for cpu and video core idle fail (-110)", runtime PM ends in `error`, and every buffer request times
  out. Reloading the modules (`modprobe -r venus_enc venus_dec venus_core; modprobe venus_core`) reloads the firmware
  without a reboot.

Did not help: keeping the decoder and `venus_core0_gdsc` powered (`power/control=on`); unloading the encoder; a
variant with the core0 clock and power together with the whole block as on msm8939, with and without the 3xx
wrapper power writes (`venus_core.v3_wrapper_ctrl=1`); TBU clocks always on (0005); the frequency table from Android.
The IOMMU context number 21 is right (`qcom,ctx-asid = <21>` for `venus_ns` in the stock DT).

If you pick it up: compare with the Android driver (msm-3.10 `msm_vidc`, `qcom,hfi = "venus"` without
`hfi-version`) at the register and HFI packet level, and enable the firmware's own debug output (`venus_fw_debug`
in the source — the debugfs `fw_level` is applied only after the firmware has started).

Software decoding works: 360p30 plays with about 1 dropped frame using ~0.9 core; 720p30 drops 139 of 900 frames.
