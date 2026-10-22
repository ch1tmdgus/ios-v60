# HackiOS V60 GUI1 candidate — 2026-10-23

On 2026-10-23 the user confirmed the V60 eventually reached the home screen
and touch responds, but very slowly. Exact startup time and frame rate were
not measured. Keep GUI1, touch5 and the existing root. GPU acceleration and
wireless support are not implemented.

## Sources and build

- Kernel upstream: `8e61187098f2c26c5daee8f9f72b6416aa64b9ab`.
- Userland tools/adapters: `906425b75e02b4f99eb5d7bb1ef7c9583472d772`.
- V60 source build: `209a4950b72c26dc5a8fa9d396039f7dfde2c920`,
  [successful CI run](https://github.com/ch1tmdgus/ios-v60/actions/runs/39812932999).
- Native SW42902 GPIO-I2C and SPMI button sources feed `/dev/redwood-touch`.
  The transport worked on touch5; the native adapter needs physical testing.
- `v60=1` enables V60 input. Redwood touch/buttons, GPU, Wi-Fi, CPU frequency,
  battery, audio, camera, haptics and flash writes remain disabled. One CPU.

The Linux workflow supplies uuid headers and dsymutil-18 and verifies actual
V60 kernel symbols. Artifacts contain source kernels and logs, no Apple userland.
`fetch_ios165.py` extracts the pinned 16.5 IPSW from Apple's CDN with ZIP member
CRC verification. `prepare_userland.py` adapts the macOS build and can rebuild
the CPU GUI adapters against the pinned Theos 16.5 SDK. `GL` must be empty,
not `0`: upstream checks whether it is nonempty.

The helper fixes an upstream `mkjobs.py` variable collision: reusing the job
filename for `RWA_PACKED16` overwrote 382 on-demand plists into one wrongly
named file. The corrected small image has 407 plist files, 406 distinct labels
(Apple's sysdiagnose alias occurs twice). Counts do not prove service operation.

The large system image is 2,492,203,008 bytes. Apple files, images containing
Apple service plists, extracted trees and device backups stay local.

## Storage and loading

Read-only recovery inspection found a 16GiB HFSX filesystem inside roughly
94GiB userdata. Primary and backup volume geometry agree. The plan places a
separate APFS image at userdata +18GiB, with a 2GiB gap after HFSX, and protects
the partition's final 1MiB. No GPT change, format or boot partition flash.
Do not reuse a plan on another device or after filesystem geometry changes.

`recovery_system.py --image SYSTEM --plan PLAN` is read-only. `--backup`
reads the exact proposed write extent into a local backup. **`--install` is
a separate persistent write**, requiring an unchanged plan and verified
backup. It validates recovery/GPT/HFS geometry, writes only the planned
extent, reads it back for SHA256 verification and rechecks GPT and both HFS
headers. Interrupted writes require a complete retry and readback before boot.

`build_gui.py` builds a new shim and SWEETXNU v2 payload with the source-built
kernel. Its read-only UFS loader validates GPT CRCs, userdata bounds, HFS
geometry, APFS header and complete image CRC. A low DMA bounce buffer feeds
CPU copies into high RAM. ABL's list is restored only after DMA stops; failure
to quiesce halts. There is no SCSI WRITE command. This path is hardware-untested.

The live DT shows contiguous RAM from `0xc0000000` to `0x280000000` across two
banks. Static reserved regions are below `0xc0000000`; dynamic reserved pools
are constrained below 4GiB. The candidate uses **4–10GiB physical RAM**. This
replaces the inherited Redmi window and still needs hardware verification.

## Validation and physical test

Host checks cover coordinate/debounce policy, CRC corruption, filesystem and
footer guards, integer overflow, destination bounds, Android image ID,
preserved DTB and segment layout. The shim compiles locally; native XNU links
in CI. QEMU does not validate V60 GPIO/SPMI/UFS, reservations or panel handoff.
Initial GUI runs reached SpringBoard/backboardd but showed the Apple logo;
check the latest local screenshot/log before claiming GUI completion.

The subsequent physical user report supersedes that initial observation:
GUI1 did reach home and touch works. The longer local startup trace shows
SpringBoard synchronously waiting through CoreServices/Foundation/XPC while
lsd registers apps and extensions, with repeated multi-second delays. This
is evidence of work progressing, not proof of an indefinite deadlock.

## perf1 comparison image

The private `v60-hackios-perf1-1cpu.img` uses the same working kernel, input
code and installed system, with `fb_hz=30 fb_filter=0 fb_threads=1 fb_queue=1`.
It targets 30Hz, replaces bilinear scaling with nearest neighbour and limits
queued frames. Text edges can be rougher. It does not claim measured 30fps,
GPU acceleration, a startup-time fix or confirmed physical speedup. QEMU
passed the 36 init and 80 surface checks without an observed kernel panic.

A separate four-core software experiment failed before console startup;
device-image builds are restricted to one core until that is resolved.
The diagnostic `sb_watch.c` and GUI2-diag image were not sent to the phone.

After an explicitly authorized and verified sidecar installation, test with
temporary `fastboot boot` of the local GUI1 image. Keep the working boot image.
The GUI command line omits `-v`; shim loading progress/errors remain visible.
