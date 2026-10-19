# V60 wireless3: supplies + external UART + QCA version query

## Confirmed previous device test

User wireless2 log: both4-byte patterns matched exactly, passed2/2 fail0,
stopped1 status0, configuration restored. This proves the XNU SE6 FIFO loopback
path; it does not prove external Bluetooth power, firmware or iOS integration.

## What wireless3 actually does

Keeps touch5 touch/buttons, quiet display and30Hz presentation target.
Wireless worker starts at90s, after touch60s. Same proven five clock gates.
Requires idle/clean UART protocol2, no DMA/GSI,32bit FIFO capacities. Checks
GPLL0 inherited600MHz/div2 and no enabled DFS; no PLL programming/vote.
Requires GPIO16..19 unowned (GPIO/input) and GPIO21 BT_EN low with low latch.

Uses live V60 DT, confirmed over read-only recovery ADB:

- RSC appsDRV2=0x18220000, TCS offset0xd00. ACTIVE2,SLEEP3,WAKE3; hiddenCONTROL1.
- cmd-db reserved memory0x80860000/0x20000. No Redmi IMEM locator assumption.
- AON+DIG both `smpa6`950mV; RFA1 `smpa5`1900mV; RFA2 `smpc8`1350mV.
- No optional BT clock/current-level properties in supplied BT node.
- Original recovery config has CONFIG_SERIAL_MSM_GENI_HALF_SAMPLING=y (16x).

Validates RSC geometry and cmd-db header/VRM entries, resolves ALL3 resources
before writing. Sends two acknowledged ACTIVE commands per rail: voltage then
enable. No mode/headroom votes, sleep/wake changes or direct SPMI writes.
Uses only an empty/idle ACTIVE TCS0/1, rejects inherited resource conflicts,
masks only its own interrupt source and restores that bit after completion.
Waits at most100ms/transaction. Any uncertain write/timeout stops all rail work,
quarantines that TCS without reset/retry, and leaves BT_EN unchanged. AOP ACKs
prove request completion, not a measured rail voltage. Shared votes are retained
for this test boot, rather than guessing other consumers' previous requests.

After successful requests and50ms settling: programs SE6 root7372800Hz using
existing GPLL0-even (M384/N15625,source6,divide1); local M/S div4 ->115200baud
with16x sampling. No PLL reprogramming. Configures8N1,FIFO8bitx4,CTS/RTS flow.
GPIO16CTS/17RTS/18TX/19RX select qup6; BT_EN21 low50ms then high50ms follows
vendor power sequence. Starts RX and transmits ONLY H4 `01 00 fc 01 19`
(QCA EDL version request). Bounded64-byte RX,2s poll, strict H4/version parser.
No firmware/NVM transfer, MAC query, HCI reset, scan, pairing or iOS BT service.

Then cancels/aborts its commands with bounded waits, drives BT_EN low100ms,
restores pin configuration and (when sequencers idle) UART/root settings. A
stuck engine stays quarantined with IRQ routing masked. An RCGR-update failure
stops before BT_EN/UART setup; uncertain clock settings are not repeatedly retried.
WLAN_EN20 and PCIe are untouched. Wi-Fi remains unimplemented.

## Expected log and test

From the extracted folder in fastboot:

```sh
fastboot boot v60-xnu-wireless3-diag.img
```

Wait3minutes, return to existing adb-root recovery and run:

```sh
python3 v60-pull-touch-log.py --out v60-wireless3-log.txt
```

Send all V60 WIRELESS lines. A successful chip response would include
`HCI result=1` and `QCA product=... patch=... ROM=... SoC=...`.
This is a one-shot hardware query, so Bluetooth does NOT stay enabled afterward.
A failing RSC/clock/UART guard is a diagnostic result, not connection success.
The image has NOT yet been device-tested. No flash operation is needed.

## Code space and validation

The exact baseline hash is still required. The V60 code now also reclaims the
adjacent unused Redmi-specific RPMh helper text up to (excluding) the existing
sweet_spmi_init entry. Module13112/13196 bytes, no BSS/unresolved imports.
Before reclaiming, builder audits2,183,965 outside executable instructions for
incoming immediate branches and loaded regions for raw pointers into the old
helper (linkedit symbol records excluded): none found. This is exact-image
patching, not proof of a matching full source rebuild or a generic kext ABI.
Existing shim,ADT,DTB and SPMI/input implementation remain outside that region.

ASan/UBSan actual C function tests cover RPMh exact votes, malformed DB/geometry,
missing/duplicate/conflicting resources, occupied TCS, completion/write failures,
stale status and timeout/no retry; UART exact baud/pins/query, power-before-reset,
RX/TX ownership, partial packets, parity/bounds/timeouts, cancel/abort and restore.
Five builder audit tests reject inbound B/BL/conditional/CBZ/TBZ and raw pointers.
Existing touch/I2C/clock tests and QEMU Apple printf ABI pass. Rebuilt wireless1,
wireless2 and all4 default touch5 images match their previous hashes.

Source: Rofik kernel7eab1a7, especially bluetooth-power.c, rpmh-rsc.c, cmd-db.c,
rpmh-regulator.c, gcc-kona.c, clk-rcg2.c, clk-alpha-pll.c, msm_geni_serial.c,
pinctrl-kona.c and qcom-geni-se.c. Existing XNU RPMh port supplied initial
ownership/completion policy, adapted to verified V60 DT/resource constraints.
