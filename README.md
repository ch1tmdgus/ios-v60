# ios-v60

**An experimental iOS 16 / XNU port for the LG V60 ThinQ 5G.**

This project is adapting Apple's open-source XNU kernel (xnu-8796, the iOS 16 generation) to the **LG V60 ThinQ 5G (`timelm`, Qualcomm SM8250 / Snapdragon 865)**. The intended userland is **iPhone X iOS 16.7.16 (20H392)**.

As of **2026-10-19**, the V60 boots a diagnostic shim through its unlocked bootloader. Framebuffer output, read-only UFS access, 16 KB MMU activation, GIC register reads and a staged GIC configuration write sequence have been observed on the device. **XNU entry, interrupt delivery and the iOS userland have not yet been demonstrated on the V60.**

> This is hardware bring-up, not a usable phone OS. The current diagnostic images boot temporarily with `fastboot boot` and do not access the iOS root or write partitions. Installing an iOS root into `userdata` would erase Android user data; no such installation has been performed as part of these V60 tests.

## Current device status

| Area | V60 status |
| --- | --- |
| Boot | LG bootloader → temporary Android boot image v2 → diagnostic shim |
| Display | Three-colour test and text console at `0x9c000000`, 1080×2460, 32-bit pixels |
| CPU | EL1 entry and feature-register reads confirmed; PSCI advertises CPU_ON support |
| MMU | 16 KB translation granule enabled, instruction/data caches enabled; code, framebuffer and GIC address translations succeeded |
| Storage | UFS LUN 0 TUR, READ CAPACITY and READ succeeded; 4096-byte blocks and GPT recognised |
| Partitions | `userdata` is `/dev/block/sda28`; observed first LBA `0x005e2e28` |
| GIC reads | Distributor, boot CPU redistributor and PPI-state reads completed with CPU IRQ masked |
| GIC writes | Staged writes completed with CPU IRQ masked; readbacks need interpretation, and not every written value was reflected |
| Interrupt delivery | Not yet verified on the V60 |
| SMP | Secondary-core startup not yet verified on the V60 |
| XNU / iOS | Not yet booted on the V60 |
| Touch, buttons, battery, USB, GPU and other peripherals under XNU | Not yet ported or validated for the V60 |

The standalone shim console is not an iOS display driver. PSCI CPU_ON support is not evidence that secondary cores have actually started.

## Device test history

| Image | Observation |
| --- | --- |
| `v60-shim-test1.img` | Three-colour display test and read-only UFS/GPT probe succeeded. The missing-payload message was expected because the ramdisk contained no XNU payload. |
| `v60-shim-test2.img` | CPU and MMU pages appeared; the device then showed a black screen and rebooted before an IRQ result was observed. Cause remains unresolved. |
| `v60-shim-test2b.img` | Completed GIC register reads after MMU activation and displayed the final result. IRQ stayed masked. |
| `v60-shim-test2b-mmu.img` | MMU-only 120-second control image available; no V60 result has been supplied. |
| `v60-shim-test2c.img` | Completed the staged GIC write sequence and displayed the final result with DAIF `0x3c0`. IRQ delivery was not tested. |
| `v60-shim-test2-smp.img` | Optional secondary-core diagnostic available; no V60 result has been supplied. |

The earlier reboot has not been attributed to a specific register write, interrupt source or watchdog. Completing the later isolated tests does not by itself establish its cause.

## Current boot path

```text
LG bootloader, unlocked
  -> fastboot boot v60-shim-test2c.img
     -> diagnostic shim at EL1
        -> 16 KB MMU setup
        -> staged GIC writes and register readback
        -> final screen with CPU IRQ still masked
```

The intended later path is:

```text
LG bootloader
  -> V60 shim
     -> V60-specific Apple Device Tree + XNU Mach-O + boot arguments
        -> XNU
           -> validated root storage and filesystem
              -> iPhone X iOS 16 userland
```

Everything after the diagnostic shim in that second path remains V60 porting work. The current images do not contain an XNU payload.

## Run the current diagnostic

Use an LG V60 with an unlocked bootloader and a host with Android platform-tools. Use the DTB and boot image settings from the same device/firmware that successfully ran test1; other variants have not been validated.

From the directory containing the image:

```sh
fastboot boot v60-shim-test2c.img
```

The diagnostic displays a `STEP` before each write and a readback after it. It does not unmask CPU IRQ, enable a timer, send an SGI, run CPU_ON, access UFS or write partitions. On normal completion it stays on the final screen.

Capture the final result. If the screen disappears or the device reboots, a recording of the last `STEP` helps locate the failure. If a blue panic screen appears, capture `STAGE`, `ESR`, `ELR` and `FAR`.

See [V60-TEST2C.md](V60-TEST2C.md) for the write sequence and [V60-TEST2B.md](V60-TEST2B.md) for the read-only and MMU-control tests.

## Build diagnostic images

Requires Python 3 and AArch64 GCC/binutils. On macOS:

```sh
brew install aarch64-elf-gcc
```

The build tool also recognises the `aarch64-linux-gnu-` prefix, or an explicit `CROSS_COMPILE` prefix. Use the proven test1 image as a private local template:

```sh
python3 tools/build-v60-diag.py \
    --template /path/to/v60-shim-test1.img \
    --out shim/out/v60-test2c \
    --mode gic-write
```

Other diagnostic modes:

```sh
# Read GIC state with IRQ masked.
python3 tools/build-v60-diag.py --template /path/to/v60-shim-test1.img --out shim/out/v60-gic-read --mode gic-read

# Enable the MMU, avoid GIC access, report progress for 120 seconds.
python3 tools/build-v60-diag.py --template /path/to/v60-shim-test1.img --out shim/out/v60-mmu-hold --mode mmu-hold

# Optional secondary-core probe; device validation is pending.
python3 tools/build-v60-diag.py --template /path/to/v60-shim-test1.img --out shim/out/v60-smp --mode smp
```

The builder preserves the template's DTB, empty compressed cpio ramdisk and boot-v2 settings. It rebuilds the shim, checks for absolute C relocations, updates the kernel size and computes the boot image ID. Build output includes `Image`, `shim.elf`, symbols and the diagnostic `.img`.

The older `cpu` mode reproduces the combined test2 diagnostic, which rebooted on the V60. Use the isolated tests for further investigation.

## Backups and device data

Keep original boot/recovery images and device calibration partitions backed up before further bring-up. The supplied partition list includes A/B boot, DTBO and vbmeta partitions, as well as `persist`, `p_persist_lg`, `modemst1`, `modemst2`, `fsg` and `fsc`. Verify the actual names and slots on the specific phone before collecting backups.

Keep partition dumps, serial numbers, calibration data and device-tree dumps out of git. The current diagnostics require no partition formatting, root-image flashing or GPT changes.

## V60 hardware facts used by the shim

| Item | Observed or device-tree value |
| --- | --- |
| SoC | Qualcomm SM8250 / Snapdragon 865 |
| Display console | `0x9c000000`, 1080×2460, stride 4320 bytes |
| GIC distributor | `0x17a00000`, size `0x10000` |
| GIC redistributors | `0x17a60000`, size `0x100000`, stride `0x20000` |
| Virtual / physical timer | INTID 27 / 30; delivery still unverified |
| Counter frequency | 19,200,000 Hz |
| PSCI | SMC method; version 1.1 reported |
| SMCCC | Version 1.1 reported |
| UFS host | `0x01d84000` |
| Restart-reason location | `0x146bf65c`; the newer diagnostic completion paths do not write it |

The live device-tree RAM ranges are:

```text
[0x080000000, 0x0bbb00000)
[0x0c0000000, 0x180000000)
[0x180000000, 0x280000000)
```

These ranges include or overlap reserved allocations. They are **not** a validated XNU memory layout. A V60 payload must account for fixed and dynamic reservations before choosing a kernel-managed region.

The supplied `dt.tar` contains the usable raw device-tree properties. The supplied `live.dts` was malformed and the supplied `dmesg.txt` was empty; neither was used as proof of hardware configuration.

## Next porting milestones

1. Review the GIC write readbacks and test interrupt delivery one source at a time.
2. Establish MMU stability and validate the intended CPU topology before enabling SMP.
3. Generate a V60-specific Apple Device Tree with verified memory, framebuffer and interrupt settings.
4. Enter XNU on one core with unsupported peripheral paths disabled.
5. Validate storage and filesystem access before attempting any iOS root installation.
6. Port display presentation, input, power and other device-specific drivers before testing the iOS interface.

No V60 home-screen, application, touch, networking, audio or camera success is claimed by this README.

## Repository layout

| Path | Purpose |
| --- | --- |
| `shim/` | Bare-metal C/assembly boot shim |
| `tools/build-v60-diag.py` | V60 diagnostic builder using the proven boot-v2 template |
| `tools/unpack_bootv2.py` | Android boot image inspection |
| `tools/mkpayload.py` | Kernel, Apple Device Tree and boot-argument packaging infrastructure |
| `tools/mkadt.py` | Existing tree-generation infrastructure; requires V60-specific adaptation before device use |
| `xnu/patches/` | Inherited XNU patch series; device-dependent portions require V60 review and adaptation |
| `.github/workflows/xnu.yml` | Existing XNU build workflow; a successful build alone does not establish V60 compatibility |
| `V60-BRINGUP.md` | Earlier V60 diagnostic setup and test2 history |
| `V60-TEST2B.md` | GIC read-only and MMU-control diagnostics |
| `V60-TEST2C.md` | Staged GIC write diagnostic |
| `NOTES.md` | Engineering handoff, observations, build results and outstanding work |

Backups, device trees, recovery images, Apple firmware, iOS root images and generated build trees are not included in the source distribution.

## Kernel references and credits

V60 hardware reference sources:

- [MomoYuuki/android_kernel_lge_sm8250](https://github.com/MomoYuuki/android_kernel_lge_sm8250)
- [rofikkerneldev/kernel_lgv600tm_4.19](https://github.com/rofikkerneldev/kernel_lgv600tm_4.19) — source identified by the user as used for the current Android kernel build

The project uses infrastructure based on Apple's open-source XNU and related components. Other inherited dependencies include BearSSL and the Darwin XNU build tooling. Apple firmware and iOS userland files are not distributed here.
