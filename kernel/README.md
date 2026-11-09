# Kernel: linux-postmarketos-qcom-msm8956 7.2.3, release 17

A pmbootstrap recipe (`APKBUILD`, config, 28 patches) on top of the postmarketOS/Nura package
`linux-postmarketos-qcom-msm8956` 7.2.3-r0 (source: [codeberg msm8956-mainline/linux](https://codeberg.org/msm8956-mainline/linux),
tag `v7.2.3-r0`). This is the kernel running on the author's kenzo since 2026-11-10.

The config is the pmaports one plus five options for audio:
`CONFIG_SLIMBUS=m`, `CONFIG_SLIM_QCOM_NGD_CTRL=m`, `CONFIG_QCOM_Q6V5_PAS=m`, `CONFIG_SND_SOC_WCD9335=m`,
`CONFIG_SND_SOC_MSM8996=m` (`SND_SOC_WCD_MBHC` is selected by patch 0024).

Some patches written here carry Russian code comments; the commit messages are in English.

## Patches

| # | what | author / origin | status |
|---|---|---|---|
| 0001–0003 | Venus video codec: DT node, kenzo memory region and firmware, driver resources for MSM8956 | this repo | **does not work yet** — firmware loads, decoding yields no frames ([docs/venus.md](../docs/venus.md)) |
| 0004 | touchscreen: wake the maXTouch over I2C SCL (it NAKs the first transfer after sleep and hung) | this repo | works |
| 0005 | gcc-msm8976: keep the Venus TBU clocks on | this repo | experiment for Venus, harmless |
| 0006 | MSM8956 DT: both Cortex-A72 (cpu4 and cpu7 — cpu5/6 do not exist), OPPs up to 1190.4 MHz, thermal trips, ramoops | this repo (the cpu4/cpu7 finding is from [zennnez/msm8956-mainline-linux](https://github.com/zennnez/msm8956-mainline-linux) d7d0c8c) | **under observation** ([docs/a72.md](../docs/a72.md)) |
| 0007 | kenzo DT: `vdd_apc1` (A72 cluster) minimum 1.05 V | this repo | under observation |
| 0008 | arm64 errata: let late (hotplugged) CPUs pass the Spectre-v2 check | this repo | diagnostic; only needed to online the A72 after boot |
| 0009–0013 | ADSP (remoteproc PAS), `RPM_SMD_DIV_CLK1`, SLIMbus + QDSP6 nodes for MSM8976, kenzo audio DT, SLIMBUS_0_RX routing | Magicxavi, [kenzo-mainline/kernel_mainline_msm8956](https://github.com/kenzo-mainline/kernel_mainline_msm8956) branch `7.2.3-kenzo` (27167ca, df079a1, df75f97, 9057074, 439744a); 0012 and 0013 changed here — MCLK from `DIV_CLK2` as in the stock DT, routing (notes in the patch headers) | works |
| 0014 | wcd9335: start the SLIMbus RX stream once the DAC is up (the cause of the silence, see [docs/audio.md](../docs/audio.md)) | this repo | works |
| 0015 | wcd9335: fix codec init (efuse sensing, MCLK config) | Lajosházi László Gergely, Fairphone 3 port ([llg179org/linux](https://github.com/llg179org/linux) 6058bfc) | works |
| 0016 | q6afe-dai: start SLIMbus AFE ports from a work item after trigger, stop them in hw_free | this repo | works |
| 0017 | slimbus: qcom-ngd-ctrl: implement `disable_stream` | Viken Dadhaniya (Qualcomm), upstream 099460e | works |
| 0018 | kenzo DT: clock the codec (MCLK) for the microphone bias supplies | this repo | works |
| 0019 | q6asm-dai: drop S24_LE (the DSP read it 256× too small, ~48 dB quieter) | this repo | works |
| 0020–0024 | shared `wcd-mbhc-v2` for WCD9335: function table, OCP interrupts, legacy comparator backend, detection logic chosen by the codec, wcd9335 on the shared MBHC | Lajosházi László Gergely, Fairphone 3 port ([llg179org/linux](https://github.com/llg179org/linux) 8e0a624, f9eb99c, d9885b8, 611b15a, 8476d68) | works (plain headphones tested) |
| 0025 | wcd9335: MBHC headset comparator reference 1.7 V (stock kenzo calibration) | this repo | works |
| 0026 | dt-bindings: document the MBHC jack properties of qcom,wcd9335 | this repo | — |
| 0027 | kenzo DT: headset jack — both switches normally closed, button thresholds | this repo | works |
| 0028 | wcd9335: do not enable a SLIMbus stream without channels (PipeWire lost the whole card otherwise) | this repo | works |

Patches marked "this repo" were written by Claude (Anthropic, Claude Opus 5.5) under the direction of Eduard Ryzhov
and carry an `Assisted-by:` line; they have no `Signed-off-by` and were not submitted anywhere. Each was tested on
one Redmi Note 3 Pro (kenzo, MSM8956, the 2 GB / 16 GB variant).

## Build

With [pmbootstrap](https://wiki.postmarketos.org/wiki/Pmbootstrap) set up for `xiaomi-kenzo` / `qcom-msm8956` (edge):

```sh
cp APKBUILD config-postmarketos-qcom-msm8956.aarch64 00*.patch \
   "$(pmbootstrap config work)/cache_git/pmaports/device/testing/linux-postmarketos-qcom-msm8956/"
pmbootstrap checksum linux-postmarketos-qcom-msm8956   # only if you changed something
pmbootstrap build linux-postmarketos-qcom-msm8956
```

The package lands in `<work>/packages/edge/aarch64/linux-postmarketos-qcom-msm8956-7.2.3-r17.apk`. On an
8-thread x86-64 machine with a warm ccache a rebuild takes about 10 minutes.

## Install

On the phone (as root, from a service so a dropped ssh session does not interrupt it):

```sh
systemd-run --unit=kernel-install --collect --wait sh -c \
  'apk add --allow-untrusted /home/user/linux-postmarketos-qcom-msm8956-7.2.3-r17.apk; sync'
reboot
```

`apk add` from a file also pins that exact package in `/etc/apk/world`, so `apk upgrade` does not replace it with the
repository build. To go back, keep the previous `.apk` and install it the same way; to return to the repository
kernel, remove the pin from `/etc/apk/world`.

Audio also needs the ADSP firmware: on postmarketOS `msm-firmware-loader` provides `adsp.mdt` from the Android
firmware partition. The userspace part (UCM, WirePlumber) is in [../audio](../audio).

## Kernel command line notes

The port ships `maxcpus=4` (the A72 were considered unstable). With 0006/0007 the author boots without it — see
[docs/a72.md](../docs/a72.md) before doing the same.
