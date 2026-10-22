# V60 HackiOS boot1 — 2026-10-22

새 XNU8796.121.2 기반 **첫 실기기 부팅을 확인하기 위한 진단 이미지**입니다.
QEMU에서 새 커널 → GICv3/타이머 → APFS RAM 루트 → EL0 PID1 실행과
1080×2460 텍스트 화면 출력을 확인했습니다. **V60 실기기 부팅은 아직 미검증**입니다.
이 이미지에서 iOS 잠금화면·터치·버튼·GPU·Wi-Fi는 실행하지 않습니다.
기존 touch5 GUI 이미지는 그대로 보관합니다.

## 시험

출력 파일은 `v60-hackios-boot1-diag.img`입니다. 기존과 같은 V60 fastboot에서:

```sh
fastboot boot v60-hackios-boot1-diag.img
```

RAM에 임시 부팅하는 명령입니다. 이 테스트에는 flash/erase/format 명령이 필요 없습니다.
이미지에 자체 진단 루트가 들어 있어 기존 iOS/Android 사용자 데이터는 마운트하지 않습니다.
성공하면 화면 아래에 다음 문구가 남고, PID1은 잠든 상태로 유지됩니다:

```text
V60 HACKIOS: PID1 PASS - XNU 8796.121.2 / APFS / arm64 syscalls
V60 HACKIOS: diagnostic root alive; iOS GUI and V60 drivers pending
```

화면을 촬영해 결과를 확인합니다. 멈추거나 재부팅되면 마지막 문구와 부팅 후 대략 몇 초인지가
필요합니다. 구형 `v60-pull-touch-log.py`는 새 커널 로그 주소/형식을 지원하지 않습니다.
이 진단에서는 화면 로그를 켰습니다. 이후 GUI 이식 이미지의 로그 표시 설정과는 별개입니다.

## 실제 변경

- 원본 V60 `v60-xnu-boot2-gui.img`의 SHA256을 검사한 뒤 Android boot v2 헤더와
  V60 DTB를 사용합니다. 기존 shim에서 boot_args의 `Video.v_display`를 기록하는
  명령 **하나만** 텍스트 모드(0)로 바꿉니다. 원래 값(1)은 새 XNU에서 검은 그래픽
  화면을 선택합니다. 원본 SHA와 명령어까지 일치해야 변경합니다.
- 새 커널의 가상 기준 `0xfffffff004000000`, 텍스트 `0xfffffff007004000`을 사용합니다.
  기존 32MB 정렬 추정값을 사용하지 않습니다. 새 XNU는 4KB 페이지이지만 shim이
  예약하는 ADT/boot_args 영역의 16KB 정렬은 유지할 수 있습니다.
- 기존 V60에서 사용한 RAM 창 `0xb0000000 + 0x4d000000`, framebuffer `0x9c000000`,
  1080×2460/stride4320, GICD `0x17a00000`, GICR `0x17a60000`, 타이머 PPI27,
  19.2MHz와 CPU0 하나만 전달합니다. Redwood의 UEFI·GPIO·전원 기본값을 가져오지 않습니다.
- `XNULDRST`의 physical/virtual delta를 설정합니다. UEFI return callback은 없으므로 0입니다.
- 자체 어셈블리 PID1을 빌드해 4MB APFS RAM 루트 `/sbin/launchd`에 넣습니다.
  getpid가 1인지 검사하고 콘솔에 성공 문구를 쓴 뒤 nanosleep으로 대기합니다.
  iOS16.7 사용자 공간을 새 16.5 커널에 혼합하지 않습니다.
- GPU/Wi-Fi/Goodix/Redwood 버튼·클럭·전원·UFS 쓰기를 켜지 않습니다.

**커널 전체를 소스 재빌드한 결과는 아닙니다.** 고정한 upstream의 plain-arm64
prebuilt kernel을 사용하며 loader delta 슬롯만 수정합니다. 공개된 커널 전체 소스는
Apple 원본8796.121.2에 upstream 패치73개/추가 파일104개를 적용해 별도 작업 트리에
준비했습니다. 이 진단이 V60에서 통과하면 새 소스의 SW42902/버튼 통합과 일치하는
iOS16.5 사용자 공간 빌드를 진행할 수 있습니다. GPU650·QCA6390은 별도 이식 단계입니다.

## 재현 빌드 (macOS / Xcode Command Line Tools)

고정한 kernel/userland 리비전은 [이식 검토](HACKIOS.md)에 있습니다.
kernel에는 `prebuilt/mach.development.bcm2837`, userland에는
`tools/apfs/mkapfs.py`가 있어야 합니다. 전체 SDK나 iOS IPSW는 이 진단에 필요 없습니다.
빌더는 커밋과 커널/APFS 빌더 해시를 검사하고, 새 출력 폴더만 허용합니다.

```sh
python3 hackios-port/build.py \
  --baseline /path/to/v60-xnu-boot2-gui.img \
  --kernel-repo /path/to/hackios-xnu-kernel \
  --userland-repo /path/to/hackios-xnu-userland \
  --out /tmp/v60-boot1-new
```

`--qemu`로 별도 빌드하면 GIC/UART/PSCI/타이머만 QEMU virt용으로 바뀝니다.
QEMU 검사는 `qemu-system-aarch64`가 필요합니다:

```sh
python3 hackios-port/smoke.py /tmp/qemu-build/payload.bin \
  /path/to/hackios-xnu-kernel/prebuilt/mach.development.bcm2837 \
  /tmp/v60-smoke-new --framebuffer
```

`smoke-result.json`은 실제 PID1 표식을 요구하고 panic을 거부합니다. QMP로 커널 콘솔
링과 framebuffer를 읽습니다. UART 없는 화면 경로도 검사하며 V60 하드웨어 성공으로
취급하지 않습니다. 두 번의 독립 빌드가 동일한 이미지 해시를 생성하는 것도 확인했습니다.

upstream 커널의 라이선스/고지는 `prebuilt/NOTICES.md`와 `LICENSE`를 보존합니다.
펌웨어·IPSW·Apple 사용자 공간·개인 파티션 덤프는 소스 저장소에 추가하지 않습니다.
