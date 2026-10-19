# V60 wireless2: internal GENI UART FIFO transport test

## Actual wireless1 result

User booted wireless1 and provided persistent log. All5 wrap0/SE6 branches
ready; SE6 fw/sfw203 (GENI UART protocol2/version3), status0, FIFO_DISABLE0,
DMA0, TX/RX FIFO status0. TX32bit/depth16, RX32bit/depth32. GPIO16..19 mux0
(GPIO), GPIO20 WLAN_EN low, GPIO21 BT_EN low, GPIO124 SW_CTRL low.
GENI microcode is not proof that the Bluetooth controller firmware is running.

## What changes in wireless2

Preserves touch5 touch/buttons, quiet display and30Hz presentation target.
Separate worker starts at90seconds. After the same bounded clock-ready gates,
requires both sequencers UART protocol,32bit FIFOs, nonzero depth, no active
commands/pending IRQs/FIFO data/DMA/GSI. If a check fails, does not write UART.

Programs SE6-internal loopback3,8N1, matching M/S divider1 on inherited root,
CTS masked and8bitx4 packing. Two different4-byte patterns go through TX FIFO,
UART sequencers and RX FIFO. Reports exact TX/RX comparison and failures.
Each wait is bounded20ms. Cancels its RX/TX commands, with bounded abort fallback,
then restores saved configuration only when idle. SE interrupt routing is masked
throughout test. No GIC changes. GPIO pins remain unmodified throughout; this
isolates UART transport from Bluetooth power and external baud setup.

Success looks like `loopback passed=2/2 fail=0 stopped=1`, followed by
`loopback restored`. A passing host model does not predict a passing device test.
This is NOT a working Wi-Fi/Bluetooth connection image. No HCI, firmware/NVM
transfer, PMIC/regulator write, BT_EN/reset, PCIe or DMA access. An inactive
Bluetooth controller cannot answer HCI merely because SE6 has UART microcode.

## Run

From the extracted folder, with the device in fastboot:

```sh
fastboot boot v60-xnu-wireless2-diag.img
```

Wait3minutes, then return to existing adb-root recovery and run:

```sh
python3 v60-pull-touch-log.py --out v60-wireless2-log.txt
```

Send V60 WIRELESS lines or full log. No flash operation is needed.

## Validation

Local cross-build and exact baseline guards passed. Module8992/9064bytes;
no BSS/unresolved imports. ASan/UBSan model tests exercise actual loopback
function for success, wrong protocol/width/FIFO mode, dirty/busy/DMA/GSI,
TX/RX timeouts, start failure, corrupt data, wrong FIFO count, error IRQ,
cancel/abort failures and saved-register restore. Existing clock-gate, touch,
GPIO I2C and QEMU Apple ABI checks pass. Rebuilt wireless1 and all4 default
touch5 images reproduce previous hashes. User device result later PASS: both patterns match, passed2/2 fail0 stopped1,
settings restored. See [WIRELESS3](WIRELESS3.md) for power/external HCI query.

## Next implementation

After actual loopback passes: implement external UART baud/pinmux and verified
shared power supply sequence, then bounded HCI ROM revision query. The supplied
V60 DT resolves AON/DIG to pm8150_s6, RFA1 to pm8150_s5 and RFA2 to pm8150a_s8.
Android configures these RPMh supplies before BT_EN. Do not skip the rail setup
or infer that they are on from UART clocks alone. Full Wi-Fi PCIe/MHI/datapath
and iOS network integration remain separate work.
