# V60 wireless1 — first XNU wireless hardware probe

This is a diagnostic boot image, not working Wi-Fi/Bluetooth connectivity.
Built against the exact original v60-xnu-boot2-gui.img SHA256 listed in manifest.
Keeps touch5 touch/buttons, quiet display, 30Hz target (not measured FPS).

## Test

Use a PC terminal in the extracted folder, with the V60 in fastboot:

```sh
fastboot boot v60-xnu-wireless1-diag.img
```

Wait at least 3 minutes after boot. The touch worker starts at60s; a separate
wireless worker starts at90s. Then return to the existing adb-root recovery
using the device's recovery procedure, and pull the persistent log:

```sh
python3 v60-pull-touch-log.py --out v60-wireless1-log.txt
```

Send the V60 WIRELESS lines or the log. No new flashing command is needed.
The image itself was built/checked locally but has not been boot-tested yet.

## Hardware changes

Votes GCC QUP wrap0 M/S AHB, core/core2x and SE6 clocks via existing vote register
0x152008, preserving other vote bits. Each branch has a bounded5ms readiness
check; only S_AHB has HWCGbit1. Stops before UART mapping/access on failure.
Does not program PLL/clock source/RCGR rate or clear/reset any hardware.
Records NORTH GPIO16..21 and124; these pins are read-only. If clocks pass,
reads GENI SE6@0x998000 protocol/clock/FIFO-capability/FIFO-status/IRQ-status registers.
Does not read RX FIFO data or clear pending IRQs. SE12 console is untouched.
No radio enable/reset, regulator, PMIC, PCIe, HCI or firmware download writes.
Wi-Fi evidence in this stage is limited to WLAN_EN/SW_CTRL GPIO state.
The next transport implementation depends on the actual XNU SE6/power results;
recovery's working Linux state does not prove the bootloader leaves UART ready.

## Host validation

ASan/UBSan simulated hardware: successful clock gate path, all5 HALT failures,
map failure, rejected vote, preserved existing bits, bounded waits and no UART
reads on failure; writes restricted to one GCC vote register. Existing packet,
I2C model and QEMU Apple-varargs bridge checks pass. Default touch5 images were
rebuilt and all4 hashes match their previous outputs. Wireless module7845/9064
bytes, no unresolved imports/BSS, baseline and exact patch guards pass.
