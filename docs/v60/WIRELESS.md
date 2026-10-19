# V60 QCA6390 Wi-Fi / Bluetooth 이식

**현재 무선 기능은 미구현이며, 수정 완료 부팅 이미지는 없습니다.**
사용자는 touch5 버튼 동작을 확인했습니다. 무선은 별도의 드라이버 작업입니다.
현재 Android는 없고 recovery만 사용할 수 있으므로, 아래 도구로 원래 펌웨어를
읽어 컨트롤러 초기화 자료를 확보하는 단계입니다.

## 확인한 하드웨어와 코드

제공된 DT의 compatible은 Wi-Fi `qcom,cnss-qca6390`, Bluetooth `qca,qca6390`입니다.

| 경로 | 확인한 내용 | 아직 확인하지 않은 것 |
| --- | --- | --- |
| Wi-Fi | CNSS2/QCA6390,PCIe RC0,MHI, WLAN_EN GPIO20 | 실제 endpoint revision/link/firmware 응답 |
| Bluetooth 전원 | BT_EN GPIO21, 여러 regulator 공급. DT와 bluetooth-power.c 대조 | 현재 전원/펌웨어 기동 상태 |
| Bluetooth UART 후보 | 활성 HS UART SE6,PA00998000,size4000,4-wire | Android vendor 설정/실제 tty와의 최종 연결 |
| SW_CTRL | CNSS DT GPIO124 | BT 단독 사용 시 해당 신호의 실제 소유/절차 |

UART SE12 a90000은 DT상 console입니다. Bluetooth라고 가정해 재설정하지 않습니다.
SE6 핀/클럭/flow control도 아직 XNU에서 제어하지 않았습니다.
현재 kernel의 심볼에서 QCA6390 PCIe/MHI 드라이버나 IO80211/IOBluetooth 계열을
찾지 못했습니다. 이 관찰만으로 사용자 공간의 모든 파일을 조사했다고 주장하지 않습니다.
`bluetoothd` 프로세스 실행/반복 실행도 Bluetooth 하드웨어 지원의 증거는 아닙니다.

참조 Android 커널: Rofik `7eab1a7189a7aba0a0c4a30ba616619e097ced52`.

- `drivers/net/wireless/cnss2/pci.c`: QCA6390 firmware major version 검사,
  `qca6390/amss20.bin` 또는 fallback 이름 선택, MHI 초기화.
- `drivers/net/wireless/cnss2/power.c`, `qmi.c`: 전원/공급과 firmware handshake.
- `drivers/staging/qcacld-3.0`, `qca-wifi-host-cmn`: Wi-Fi host/datapath 기반.
- `drivers/bluetooth/bluetooth-power.c`: QCA6390 regulator/rfkill 전원 경로.
- `drivers/tty/serial/msm_geni_serial.c`, `kona-qupv3.dtsi`, `kona-pinctrl.dtsi`: GENI UART.

Linux 드라이버는 XNU에 그대로 링크할 수 없습니다. Wi-Fi에는 PCIe host/PHY,
SMMU/DMA/MSI, MHI firmware boot, QMI/WMI/HTT/datapath와 네트워크/사용자 공간 연결이
필요합니다. Bluetooth에는 공유 전원, UART/flow control, revision에 맞는 firmware/NVM,
HCI transport와 iOS bluetoothd 연결이 필요합니다. 현재 약2.8KiB 남은 터치 모듈 공간에
기능을 켜는 패치로 해결하는 범위를 넘습니다. 가짜 Wi-Fi/BT 서비스를 등록하지 않습니다.

## Recovery에서 자료 수집

기존 adb-root recovery에 연결한 상태에서 실행합니다:

```sh
python3 tools/v60-wireless-collect.py --include-firmware --mount-vendor-ro --out v60-wireless.zip
```

배포된 단일 파일은 `python3 v60-wireless-collect.py ...`로 실행할 수 있습니다.

- 이미 보이는 firmware 경로를 읽습니다. 필요하면 현재 A/B slot의 vendor와
  modem firmware filesystem을 recovery `/tmp/v60-wireless-*`에 임시 마운트합니다.
- ext4는 `ro,noload`, erofs/vfat는 `ro`입니다. journal replay/파티션 쓰기는 하지 않습니다.
- slot이 확인되지 않으면 `_a`/`_b` 중 하나를 추측하지 않습니다.
  이미 제공된 unsuffixed mapper/by-name 장치만 후보로 사용합니다.
- 종료/예외 시 임시 mount 해제와 빈 디렉터리 제거를 시도합니다.
  `cleanup_errors`가 있으면 임시 경로를 확인하거나 recovery를 재부팅합니다.
- DT의 무선 관련 속성, PCI ID, tty 포트 이름, 제한된 build/칩 property,
  후보 firmware 파일과 SHA256을 보관합니다.
- radio ON/OFF,UART/HCI 명령,PCIe/DMA register 접근,flash는 실행하지 않습니다.
- `/persist`,Wi-Fi 설정/암호,MAC 파일,일반 파티션 덤프는 수집하지 않습니다.
  firmware symlink가 허용 경로 밖으로 벗어나면 읽지 않습니다.
- 파일당16MiB/합계96MiB 제한. 기존 출력 ZIP은 덮어쓰지 않습니다.

2026-10-20 실제 recovery에서 modem을 읽기 전용 마운트해 Wi-Fi firmware/board
파일30개를 수집했고, ZIP 무결성과 임시 mount 해제를 확인했습니다. 추가로 현재 slot의 `super` metadata와 vendor ext4의 필요한 범위만 읽어
`htbtfw20.tlv`(202300 bytes), `htnv20.bin`(5775 bytes)도 확보했습니다.
두 TLV의 구조/길이를 검사했습니다. 파일의 ROM build는0x200이지만 실제 칩의
HCI revision과 일치하는지는 아직 확인하지 않았습니다. NVM 값은 비공개로 유지합니다.
현재 recovery에는 vendor logical block 장치가 없어 이 단계는 별도의 로컬 분석으로 수행했습니다.
`recovery` ADB 상태를 허용하고, 원격 실패 상태를 전달하는 `adb shell -T`를 사용하며,
독립 `blkid`가 없으면 `toybox blkid`를 사용합니다. 관련 모의 검사15개도 통과했습니다.
firmware가0개면 `report.json`의 mount 결과/경로를 확인합니다.

생성된 ZIP은 다음 개발에 필요한 **비공개 입력 자료**이며 Git/Release에 올리지 않습니다.
원래 vendor가 남아 있고 recovery가 파일시스템을 지원해야 합니다. `vendor_b` 같은 이름을
하드코딩해서 비활성 slot이나 다른 파티션에 접근하지 않습니다.

## 다음 단계

1. 확보한 BT/Wi-Fi 파일을 사용하되, 실제 HCI revision과 Wi-Fi board ID를 먼저 확인합니다.
2. Bluetooth 전원·GENI UART를 분리해 구현하고 HCI revision 응답부터 확인합니다.
3. 별도로 Wi-Fi PCIe endpoint/MHI boot를 구현한 후 실제 firmware handshake를 검증합니다.
4. transport 성공 이후에 각각 iOS 서비스/네트워크 연결을 구현합니다.

스캔/페어링/연결 성공은 아직 없습니다. 현재 동작하는 touch5 이미지는 그대로 유지합니다.

확보한 Bluetooth 파일의 구조 확인은 아래 도구로 오프라인에서 할 수 있습니다.
NVM은 tag ID와 길이만 출력하며 MAC 등 tag 값은 출력하지 않습니다.

```sh
python3 tools/v60-bt-firmware-info.py /private/path/htbtfw20.tlv
python3 tools/v60-bt-firmware-info.py /private/path/htnv20.bin
```
