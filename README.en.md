[![한국어](https://img.shields.io/badge/Language-한국어-blue?style=for-the-badge)](README.md)

# iOS 16 on LG V60

An experimental **iOS 16 / XNU porting project** for the LG V60 ThinQ 5G
(`timelm`, Qualcomm SM8250 / Snapdragon 865), bringing the existing `ios-sweet`
work to the V60.

**2026-10-19: iOS GUI boot and working touch input with touch4 have been confirmed on a V60.**
**Button operation has also been confirmed by a user** with the latest touch5.
There are no quantitative results for input latency or frame rate improvements yet.
The black wallpaper and freezing when opening Settings → General → About remain unresolved.

Work on the [QCA6390 Wi-Fi/Bluetooth port](docs/v60/WIRELESS.md) is ongoing.
2026-10-20: Firmware acquisition and on-device verification of internal SE6 UART
transmission and reception are complete.
[wireless3](docs/v60/WIRELESS3.md) is the next test: it requests power, then queries
the external Bluetooth chip's version. On-device results are still pending.
Wi-Fi connectivity and Bluetooth pairing are not implemented.

## Downloads

[Experimental builds and checksums — Releases](https://github.com/ch1tmdgus/ios-v60/releases/tag/v60-bringup-20261019)

| Purpose | File | Verification status |
| --- | --- | --- |
| Version with confirmed working touch input | `v60-xnu-touch4-gui.img` or `v60-touch4-bundle.zip` | User-reported success on a physical device |
| Latest standard GUI build | `v60-xnu-touch5-gui.img` | Button operation confirmed; quantitative performance validation pending |
| Latest diagnostic build, with on-screen logs hidden | `v60-xnu-touch5-inspect.img` | For diagnosing the About screen freeze and buttons |
| CPU load comparison | `v60-xnu-touch5-balanced.img` | touch5 input improvements + a 30 Hz target for screen copying |
| Earlier shim/touch stages | `v60-shim-test*-bundle.zip`, `v60-touch1..3-bundle.zip` | See the history below |

Each bundle contains the corresponding image, replacement module source,
documentation, and checksums. Verify downloads using `SHA256SUMS.txt` in the release.
Apple IPSW files, iOS root images, and user partition/calibration dumps are not distributed.
This boot image alone does not install the iOS userspace.

## Current Status

| Component | V60 status |
| --- | --- |
| Boot | XNU/iOS GUI boot confirmed by a user with the supplied `v60-xnu-boot2-gui.img` |
| Touch | **touch4 works**: SW42902, GPIO software I²C → existing HID path |
| Touch communication | GENI SE13 confirmed to have FIFO disabled and operate in GSI-only mode. GPIO36/37 are used instead of porting GSI DMA |
| touch5 input improvements | Single-finger reports reduced from 156 to 48 bytes; additional contacts read only as needed; polling interval reduced from 10 to 2 ms. On-device logs confirm 48-byte/contact reads; total latency has not been measured |
| Power/volume buttons | Operation confirmed by a user in touch5 using the existing SPMI observer driver and V60 mappings |
| On-screen logs | Hidden after the GUI appears in touch5. Persistent logs are retained, and panics can still be displayed |
| Frame rate/responsiveness | CPU framebuffer copying. touch5 targets 60 Hz by default, with a 30 Hz comparison build. Actual FPS has not been measured; GPU acceleration is not implemented |
| Wallpaper | Remains black; unresolved |
| Settings → General → About | Freezes or becomes unresponsive when opened. Cause unknown |
| CPU/SMP | Preserves `cpus=4` and the ADT from the working image. Per-core operation and eight-core support have not been separately verified |
| UFS/root | Read-only UFS/GPT access verified in the shim, followed by a user report of userspace booting. No general installation procedure is provided |
| Battery | The displayed icon is not evidence of actual fuel gauge readings. V60 hardware integration is unverified |
| Bluetooth port | wireless2 internal UART transmission/reception succeeded. wireless3 is a power/HCI version query build; on-device results pending, pairing not implemented |
| USB/networking/audio/camera/GPU | No verified V60 support |

The camera and battery icons on screen do not indicate that the corresponding hardware works.
Earlier successes on Redmi devices are kept separate from V60 results.

## Testing

Keep a copy of the working original/touch4 image and **temporarily boot** it on an
unlocked V60. The current diagnostic image is:

```sh
fastboot boot v60-xnu-touch5-inspect.img
```

1. Wait at least 60 seconds after booting. The touch worker starts after this delay.
2. Test tapping the center of the screen, swiping from the bottom, scrolling, both volume buttons, and a short press of the power button.
3. Leave Settings → General → About frozen for about two minutes.
4. Return to your existing recovery environment with adb root access and retrieve the logs:

```sh
python3 v60-touch/pull_log.py --out v60-touch5-log.txt
```

`v60-pull-touch-log.py` in the release bundle is the same tool. Use the **complete
saved log** to investigate the Settings and wallpaper issues. The extractor only
reads the existing 4 MiB log at the end of userdata.
Do not expect an adb connection while XNU is running, because `sweetusb=0`.

Even the inspect build hides on-screen logs after the GUI appears. Initial boot
text may appear briefly. Stack samples in inspect are recorded after about 90
seconds, then every 120 seconds. Use `touch5-gui` for general usage comparisons;
if it stutters more, try `touch5-balanced`.

The existing image's filesystem write options are unchanged. A temporary boot
does not mean the iOS root is read-only. This repository's builder and log
extractor do not flash partitions.

## How the Images Are Made

After checking the symbols and fixed virtual addresses in the original GUI boot
image, the inactive touch module's space is replaced with standalone code from
`v60-touch/`. Three HID coordinate constants, two touch5 button constants, and
three diagnostic strings are also patched after verifying their original values.
In wireless3, adjacent unused Redmi-specific RPMh code space is also used for
V60 power/UART code after checking references.
The shim, DTB, ADT, memory layout, and CPU layout are preserved.

**The complete XNU build source/commit matching the working boot2 image has not yet been identified.**
This repository contains the base XNU patches and V60 changes, but does not claim
to have reproduced the same boot2 image by rebuilding the entire kernel.
The latest test images require the following **exact original image**:

```text
v60-xnu-boot2-gui.img
SHA-256 6c916223cf86aca1caf13d4e490a20c519dfc7f315671bb494e643a2d31ab816
Darwin 22.6.0 / xnu-8796.141.3
```

The builder rejects other inputs, including output images from touch1–5.
Do not apply this fixed-address module to a different kernel, slide, or device.
The source alone does not automatically produce the complete original kernel.

## Building Touch Images

Python 3 and AArch64 GCC/binutils are required. Host checks also use clang and QEMU.

```sh
# macOS
brew install aarch64-elf-gcc qemu

python3 v60-touch/test.py --out work/touch-tests
python3 v60-touch/build.py /path/to/v60-xnu-boot2-gui.img --out work/touch5
```

On Linux, you can set `CROSS_COMPILE=aarch64-linux-gnu-`.
Version-specific implementations are preserved in Git history and the
[source version table](docs/v60/HISTORY.md).
See [v60-touch/README.md](v60-touch/README.md) for the latest code and testing procedure.

Checks cover report parsing and length validation under ASan/UBSan, an independent
I²C mock slave, the printf ABI under QEMU, module size/symbols/patch ranges,
boot image IDs, and checksums.
Passing host/QEMU checks and succeeding on a physical V60 are separate results.

For the full base XNU build process, see [xnu/README.md](xnu/README.md).
For the initial V60 porting tools, see [V60-PORT.md](V60-PORT.md).
The `SWEET` machine name and existing symbols are build identifiers in the base
code; do not simply replace them with `V60`.

## History and Hardware References

- [Progress and verification history for shim test1–2c and touch1–5](docs/v60/HISTORY.md)
- [V60 hardware addresses and currently unverified items](docs/v60/HARDWARE.md)
- [Initial shim bring-up](V60-BRINGUP.md), [test2b](V60-TEST2B.md), [test2c](V60-TEST2C.md)
- [touch4 experiment notes](docs/v60/TOUCH4.md), [touch5 documentation](v60-touch/README.md)
- [Per-version manifests / SHA-256 checksums](docs/v60/releases/)

## Repository Layout

| Path | Contents |
| --- | --- |
| `v60-touch/` | Latest SW42902/GPIO I²C module, builder for the specific original image, host/QEMU checks, and log extractor |
| `shim/` | Base V60 port and staged CPU/MMU/GIC diagnostic code |
| `tools/build-v60-diag.py` | Generates diagnostic images using the supplied test1 image as a template |
| `tools/` | ADT/payload/Android boot image tools and existing development tools |
| `xnu/patches/` | Base XNU patches and existing V60 changes. On-device reproduction of a full build remains a separate task |
| `patches/v60/` | Originally supplied V60 shim patch |
| `docs/v60/` | Observations, version history, hardware documentation, and manifests |
| `NOTES.md` | Current V60 handover notes and separately marked historical upstream records |

Do not commit raw DT/partition dumps, serial numbers, personal logs, or Apple userspace files.
The incorrectly named `live.dts` was removed because it was an identical copy of
`apply-v60-port.py`, not an actual device tree. Actual device tree dumps are not
included in the build source.

## Base Project and Sources

- [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet) — XNU, boot, display, and HID foundation
- [MomoYuuki/android_kernel_lge_sm8250](https://github.com/MomoYuuki/android_kernel_lge_sm8250)
- [rofikkerneldev/kernel_lgv600tm_4.19](https://github.com/rofikkerneldev/kernel_lgv600tm_4.19) — User-provided Android kernel build references and SW42902/GPIO/clock/SPMI cross-checks
- [Apple XNU](https://github.com/apple-oss-distributions/xnu)
- [blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build)

The replacement touch module is licensed under GPL-2.0-only and includes the
[license](v60-touch/COPYING). Individual license and copyright notices for the
base code and third-party components are retained.
