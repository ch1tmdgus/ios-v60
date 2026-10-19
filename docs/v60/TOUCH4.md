# V60 SW42902 touch4 — GPIO 소프트웨어 I²C 실험

**touch3 실기기 로그로 GENI의 GSI 전용 상태가 확인됐습니다. touch4는 GPIO
소프트웨어 I²C로 칩 통신을 시도하는 새 버전이며 실제 터치 동작은 미검증입니다.**

대응하는 전체 XNU 소스는 확보하지 못했습니다. 특정 원본 이미지의 고정 주소에
기존 비활성 터치 모듈을 교체하고 HID 좌표 상수 세 개를 보정하는 방식입니다.
전체 XNU 재빌드나 GSI DMA 이식 완료 결과가 아닙니다.

## 달라진 통신 경로

실기기에서 확인한 touch3 값:

```text
SE13 fw=303 status=0 tx=20102864 rx=20204800
FIFO_IF_DISABLE=1 tx_width=32 tx_depth=16 rx_width=32 rx_depth=32
engine blocked reason=2
```

I²C 펌웨어(protocol=3)는 있으나 FIFO 인터페이스가 비활성화되어 있습니다.
V60 커널의 `i2c-qcom-geni.c:geni_i2c_runtime_resume()`는 이 경우 GSI_ONLY를
선택합니다. `drivers/dma/qcom/gpi.c`는 DMA 링, 이벤트 링, DMA 메모리 매핑을
사용합니다. 그 경로의 XNU 이식은 아직 구현하지 않았습니다.

touch4는 핀 컨트롤러가 지원하는 GPIO 모드로 같은 두 버스 핀을 제어합니다.
SDA36/SCL37 배선과 외부 풀업은 실기기 응답으로 추가 확인해야 합니다.

- GENI master/slave가 idle일 때만 GPIO36/37을 mux0로 설정.
- OUT 래치를 LOW로 두고 OE만 전환. HIGH는 출력하지 않고 입력으로 해제.
- 내부 풀업을 켜지 않음. 외부 풀업이 없거나 SDA/SCL이 LOW면 중단.
- START, 주소 0x28, ACK, 반복 START, 읽기, 마지막 NACK, STOP 구현.
- 각 지연은 5µs 기준. MMIO/스케줄링 때문에 실제 속도와 보고 주기는 더 느림.
- SCL 대기 최대 1000회, 회당 10µs 지연. IRQ/preemption 시간은 별도.
- arbitration loss/timeout 때 버스를 해제하고 중단. 강제 복구 펄스 없음.
- NACK에는 STOP 시도. SDA/SCL 설정과 출력 래치는 worker 종료 시 복원.
- GENI/GSI/FIFO 설정 및 DMA/IOMMU 레지스터는 쓰지 않음.
  기존 GCC 클럭 vote/준비 확인은 GENI idle 상태를 읽기 위해 유지.

V60 커널 `sw42902_power(POWER_ON)` 순서로 VCL67 HIGH, 1ms 뒤 VDD68 HIGH,
10ms 뒤 RESET38 HIGH를 적용하고 100ms 대기합니다. reset LOW는 하지 않습니다.
칩 ID=10/protocol=4가 확인되어야 동작 설정과 터치 보고서 읽기로 진행합니다.
GPIO 전원·배선·펌웨어 기동 상태는 이번 실기기 테스트에서 확인해야 합니다.

## 실기기 테스트

정상 부팅하던 원본을 보관하고 다음 이미지로 임시 부팅합니다:

```sh
fastboot boot v60-xnu-touch4-diag.img
```

기본 60초 뒤 driver가 시작합니다. 60초 이상 기다린 뒤 중앙 탭과 하단
스와이프를 시험하고, 기존 adb root recovery로 돌아와 로그를 추출합니다:

```sh
python3 v60-pull-touch-log.py --out v60-touch4-log.txt
```

로그 예시(아직 관찰하지 않은 성공 경로):

```text
V60 TOUCH: touch4 GPIO-I2C SW42902 driver delay=60 mode=1
V60 TOUCH: GPIO transport SE13 status=0 FIFO_IF_DISABLE=1
V60 TOUCH: power reset=... irq=... vcl=... vdd=...
V60 TOUCH: GPIO-I2C SDA36=1 SCL37=1 half_delay=5us
V60 TOUCH: probe 0x28 rc=0 version=... phase=5
V60 TOUCH: polling 0x600, 10ms; finger events -> HID
V60 TOUCH: contacts=1 frames=...
```

`rc=-10`: SDA idle LOW / STOP 후 버스 LOW. `-11`: SCL timeout.
`-12`: NACK. `-13`: 보내려던 1이 LOW로 읽힘(arbitration/배선/제어 문제).
`-14`: 잘못된 소프트웨어 전송 인수.
phase는 16진수: 1 START, 2 주소쓰기, 100+i 포인터/데이터 i번째,
3 반복 START, 4 주소읽기, 200+i 수신 i번째, 5 STOP입니다.
NACK 뒤 STOP 실패 시 rc는 STOP 오류를 표시하고 phase는 원래 실패 위치입니다.

`GPIO mux/OE readback failed`이면 핀 설정이 적용되지 않아 중단한 것입니다.
`probe rc=0`이어도 identity가 다르면 초기화하지 않습니다.
`contacts`가 보이는데 UI가 반응하지 않으면 이후 HID 경로를 조사합니다.

`v60-xnu-touch4-gui.img`는 같은 동작에서 콘솔 표시만 끕니다.
`v60-xnu-touch4-probe.img`는 칩 ID/제품 문자열 읽기 뒤 종료합니다.
probe도 클럭·핀·전원 설정을 변경하므로 완전한 읽기 전용 검사는 아닙니다.
로그 추출 스크립트는 userdata 끝의 기존 로그 영역을 읽기만 합니다.
부팅 이미지의 기존 iOS 파일시스템 동작 설정은 원본 그대로입니다.

## 하드웨어와 코드 출처

제공된 dt.tar의 활성 노드는 `soc/i2c@a94000/sw42902`, `lge,sw42902`,
주소0x28이고 SW42000 노드는 disabled입니다. GENI SE13는 wrapper1 S5입니다.
South TLMM: SDA36/SCL37, reset38/IRQ39. North TLMM: VCL67/VDD68.
좌표 범위는 1080×2460이며 기존 2436행 논리 화면의 12행 crop에 맞춥니다.

Rofik 커널 commit `7eab1a7189a7aba0a0c4a30ba616619e097ced52`의 다음 코드를 대조했습니다:

- `drivers/input/touchscreen/lge/lgsic/sw42902/touch_sw42902.[ch]`
- `drivers/input/touchscreen/lge/touch_i2c.c`
- `drivers/pinctrl/qcom/pinctrl-kona.c` (mux/입출력/OE 비트와 타일)
- `arch/arm64/boot/dts/vendor/qcom/kona-pinctrl.dtsi` (GPIO36/37, 2mA, bias-disable)
- `drivers/i2c/algos/i2c-algo-bit.c` (START/STOP/ACK/clock stretch 순서 대조)
- `include/linux/qcom-geni-se.h`, `drivers/i2c/busses/i2c-qcom-geni.c`
- `drivers/dma/qcom/gpi.c`, `drivers/clk/qcom/gcc-kona.c`, `clk-branch.c`

저장소: https://github.com/rofikkerneldev/kernel_lgv600tm_4.19
대체 모듈 소스와 GPL-2.0 라이선스를 포함합니다.

## 빌드와 검증

입력은 반드시 원본 `v60-xnu-boot2-gui.img`여야 합니다. 이전 touch 이미지는
입력으로 받지 않습니다. 원본 SHA-256:

```text
6c916223cf86aca1caf13d4e490a20c519dfc7f315671bb494e643a2d31ab816
```

Python3, clang, AArch64 GCC/binutils, QEMU가 필요합니다.
`CROSS_COMPILE`로 도구 접두어를 지정할 수 있습니다(기본 aarch64-elf-).

```sh
python3 v60-touch/test.py --out work/touch-tests
python3 v60-touch/build.py /path/to/v60-xnu-boot2-gui.img --out work/touch-build
```

검증 범위:

- 독립 edge 기반 모의 슬레이브: 포인터 반복 START 읽기, 156바이트 보고서,
  6바이트 설정 쓰기, 4가지 위치의 NACK, SDA stuck, SCL 초기/전송 중 timeout,
  arbitration, 일시적 SCL 대기, 인수 범위 검사. ASan/UBSan 사용.
- 보고서 decoder 15개, HWCG 판정 11개, 이전 GENI 판정 11개 회귀 검사.
- Apple arm64 printf varargs 브리지의 stack/FP/LR 보존 QEMU 테스트.
- 기존 모듈 공간 안에 배치, 미해결 심볼/전역 data/BSS 없음 확인.
- 변경 바이트를 모듈/부팅옵션/HID 명령어 세 개로 제한하고 shim/DTB/ADT 유지.
- boot image ID, 이미지 해시 및 묶음 파일 무결성 검사.

전원 GPIO와 실제 전기적 동작, 터치 칩 ACK/ID, 실제 터치/HID는 호스트 검사로
검증할 수 없습니다. 펌웨어 업로드/erase/PMIC/RPMh/reset LOW/기기 플래시는
이번 코드에 포함하지 않습니다. 절전·펜·듀얼스크린 지원도 아직 없습니다.
