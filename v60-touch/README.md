# V60 touch5 — 버튼 · 입력 지연 · 화면 로그

사용자가 touch4에서 **실제 터치 동작**을 확인했습니다. touch5는 동작한 GPIO
소프트웨어 I²C 경로를 유지하며 입력 통신량·폴링과 버튼 설정을 변경합니다.
**사용자가 touch5 버튼 동작을 확인했습니다. 성능의 정량 검증은 아직 없으며,
검은 배경화면과 정보 화면 멈춤은 아직 해결하지 않았습니다.** 후자는 조용한 진단 이미지로 로그를 수집해야 합니다.

전체 XNU 소스가 없는 상태에서 특정 원본의 비활성 터치 모듈을 교체하는
방식입니다. GSI DMA/GPU 드라이버를 구현하거나 커널 전체를 재빌드한 결과는 아닙니다.

## 변경점

- V60 커널 `sw42902_irq_read_data()`의 flexible report 방식을 적용.
  header12+pen24+첫 접점12 = 48바이트를 읽습니다. 정상 접점이 2개 이상이면
  `0x60c`에서 필요한 나머지만 읽습니다. 10접점이면 총156바이트입니다.
  비정상/debug packet에서는 vendor와 동일하게 full extra를 읽을 수 있습니다.
- 한 손가락 전송의 payload는 156→48바이트(약69% 감소). 실제 속도 향상 수치는
  아직 측정 전입니다. I²C bit delay5µs는 동작한 touch4와 동일합니다.
- 보고서 뒤 폴링 대기 10→2ms. 처음 세 보고서에 bytes/bus_us를 기록합니다.
- 기존 커널의 SPMI observer 버튼 드라이버를 활성화(`sweetkeys=1`).
  전원/볼륨다운은 SID0 PON@0810 bit0/1. 볼륨업은 V60 named pinctrl state
  `pm8150@0/pinctrl@c000/key_vol_up`의 GPIO6(c510, PPID0c5)로 변경.
  기존 Redmi GPIO mapping은 SID4 GPIO2(c110, PPID4c1)였습니다.
  APID 검색 상수 명령어 두 개와 진단 문자열 세 개를 검증하여 패치합니다.
  기존 observer READ 명령만 사용하며 PMIC 설정/리셋/IRQ clear는 쓰지 않습니다.
  GPIO input/enabled 조건이 맞지 않으면 볼륨업 입력은 보류하고 기록합니다.
- GUI 표시 후 화면 콘솔을 숨깁니다(`sweetfb_noconsole=1`).
  영구 로그와 panic 출력은 유지합니다. 초기 부팅 문자는 잠깐 보일 수 있습니다.
- 기존 CPU framebuffer 복사의 목표 상한을30→60Hz로 설정합니다.
  기존 timer는 복사 완료 후 다음16.667ms를 예약하므로 실제60fps 보장은 아닙니다.
  GPU 가속은 추가하지 않았습니다. 원본의 CPU4개/메모리/ADT/패널 설정은 유지합니다.
- 기본 GUI는 IOKit trace와 주기적 stack sampler를 끄고 추가 kprintf spam
  옵션을 제거합니다. inspect는 화면을 조용히 유지하며 Preferences trace와
  Preferences/SpringBoard/backboardd sampler를 켭니다.

## 우선 테스트: 화면 로그 없는 진단 이미지

정보 화면 멈춤과 배경화면 원인 수집까지 하려면 다음 이미지로 시작합니다:

```sh
fastboot boot v60-xnu-touch5-inspect.img
```

1. 부팅 후60초 이상 기다려 터치를 활성화합니다.
2. 볼륨 두 버튼과 전원 짧게 누르기를 시험합니다. 볼륨 UI/잠금 반응을 확인합니다.
   오디오 하드웨어 드라이버는 이번에 추가하지 않았으므로 실제 음량 출력은 별도입니다.
3. 스크롤/스와이프 반응을 touch4와 비교합니다.
4. 설정 → 일반 → 정보를 눌러 멈춘 상태로2분 정도 유지합니다.
   sampler는 커널 시작 약90초 뒤 첫 회, 이후120초 간격으로 기록합니다.
5. 기존 adb root recovery로 돌아와 새 추출기로 전체 로그를 저장합니다:

```sh
python3 v60-pull-touch-log.py --out v60-touch5-log.txt
```

화면에는 로그를 표시하지 않아도 추출할 수 있습니다. 출력된 입력 줄 외에
**v60-touch5-log.txt 전체 파일**이 정보/배경화면 분석에 필요합니다.
새 추출기는 V60 TOUCH, SWEET KEYS 및 Preferences 샘플 줄도 요약 출력합니다.

| 이미지 | 화면 로그 | 복사 목표 | 프로세스 진단 |
| --- | --- | --- | --- |
| touch5-gui | GUI 표시 후 숨김 | 60Hz | 끔 |
| touch5-balanced | GUI 표시 후 숨김 | 30Hz | 끔 |
| touch5-inspect | GUI 표시 후 숨김 | 60Hz | Preferences IOKit / 3개 프로세스 stack |
| touch5-probe | GUI 표시 후 숨김 | 30Hz | 터치는 ID/제품 읽기 뒤 종료; 버튼 활성 |

60Hz에서 더 버벅이면 `v60-xnu-touch5-balanced.img`와 비교합니다.
이미지마다 touch5의 새 입력 최적화와 버튼 mapping은 같습니다.
정상 동작한 touch4는 그대로 보관하고 모두 temporary fastboot boot로 사용합니다.

## 로그 해석과 남은 문제

`SWEET KEYS: key=0/1/2 down=...`가 전원/볼륨업/볼륨다운입니다.
`gpio0-apid`가 valid이고 `gpio0:c510`이 버튼에 따라 변하는지 확인합니다.
APID가65535 또는 read error면 observer/ownership/PMIC 상태를 더 조사해야 합니다.

`V60 TOUCH: report bytes=48 bus_us=...`는 실제 읽기 시간을 보여줍니다.
`contacts`는 터치 접점 상태이며 화면 fps 측정값은 아닙니다.

검은 배경화면은 사용자 공간 wallpaper 설정/자산/렌더링 경로가 필요할 수 있습니다.
현재 단정할 원인이나 대응하는 root 파일이 없습니다. 기기 정보는 사용자 보고상
앱 종료가 아니라 진입 시 멈춤입니다. 이전 Redmi의 용량 조회 crash를 같은
원인으로 간주하지 않습니다. inspect 로그에서 실제 대기 thread와 IOKit 요청을
확인한 뒤 수정해야 합니다. 현재 연결된 adb 장치는 없었으며 root 파일을
읽거나 수정하지 않았습니다.

## 동작한 GPIO 통신 경로

GSI-only(`FIFO_IF_DISABLE=1`)는 touch3에서 확인했습니다. touch4는 GENI idle을
확인한 뒤 SDA36/SCL37을 GPIO로 전환해 동작했습니다. LOW 출력 또는 high impedance
해제만 사용하고, 외부 풀업에 맡깁니다. clock stretching/NACK/arbitration 오류는
bounded poll로 처리합니다. worker 종료 시 두 버스 핀의 원래 설정을 복원합니다.
VCL67→VDD68→RESET38 HIGH 순서이고 reset LOW는 하지 않습니다. 칩 ID10/protocol4
확인 뒤에만 터치 동작 설정을 적용합니다. 절전/펜/듀얼스크린은 미지원입니다.

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
- 보고서 decoder 15개, flexible report planning/길이9개, HWCG 판정11개,
  이전 GENI 판정11개. 모의 slave로48+12바이트 두 번 읽기 및60c 포인터도 확인.
- Apple arm64 printf varargs 브리지의 stack/FP/LR 보존 QEMU 테스트.
- 기존 모듈 공간 안에 배치, 미해결 심볼/전역 data/BSS 없음 확인.
- 변경 바이트를 모듈/부팅옵션/HID 명령어3개/버튼 상수2개/진단 문자열3개로
  제한하고 shim/DTB/ADT 유지.
- boot image ID, 이미지 해시 및 묶음 파일 무결성 검사.

touch4의 실기기 터치는 사용자가 확인했습니다. touch5의 버튼 동작도 사용자 확인됐습니다. 실제 지연/fps 변화는 아직 정량 측정하지 못했습니다. 펌웨어 업로드/erase/PMIC/RPMh/reset LOW/기기 플래시는
이번 코드에 포함하지 않습니다. 절전·펜·듀얼스크린 지원도 아직 없습니다.
