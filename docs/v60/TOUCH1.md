# V60 SW42902 touch1 — 실기기 시험용

현재 상태는 **코드 작성·빌드·정적 검사 완료, V60 실기기 터치 미검증**입니다.
전체 XNU 소스가 없어 커널 전체를 재빌드한 결과가 아닙니다. 제공된 특정
`v60-xnu-boot2-gui.img`에서 심볼을 확인한 뒤, 기존의 비활성 터치 모듈을
이 폴더의 코드로 교체하고 HID 좌표 상수 세 개를 보정한 실험 이미지입니다.

## 확인한 원인과 하드웨어

원본 이미지의 부팅 옵션은 `sweettouch=0`이며 기존 모듈은 Redmi용
Goodix/Focaltech I2C 코드입니다. 이를 단순히 켜는 방식은 사용하지 않았습니다.

제공한 dt.tar의 활성 노드:

- `soc/i2c@a94000/sw42902`: status `ok`, compatible `lge,sw42902`, 주소 0x28.
- `sw42000@28`은 `disabled`.
- GENI SE13: 0x00a94000, wrapper1 S5.
- South TLMM: reset GPIO38, IRQ GPIO39, I2C GPIO36/37.
- North TLMM: VCL GPIO67, VDD GPIO68.
- 입력 범위: max_x=1080, max_y=2460. 끝점은 픽셀 인덱스 범위로 제한.

Rofik 커널 7eab1a7189a7aba0a0c4a30ba616619e097ced52에서 다음을 대조했습니다:
`drivers/input/touchscreen/lge/lgsic/sw42902/touch_sw42902.[ch]`,
`touch_i2c.c`, `drivers/clk/qcom/gcc-kona.c`,
`drivers/pinctrl/qcom/pinctrl-kona.c`, `kona-qupv3.dtsi`.

참조 저장소: https://github.com/rofikkerneldev/kernel_lgv600tm_4.19
드라이버 소스와 GPL-2.0 라이선스는 이 묶음에 포함합니다.

## 먼저 실행

정상 부팅됐던 boot2 이미지를 보관하고, **임시 부팅**으로 시험합니다:

```sh
fastboot boot v60-xnu-touch1-gui.img
```

드라이버는 커널 초기화 후 기본 60초를 기다렸다가 시작합니다.
기존 화면이 나타난 뒤에도 잠시 기다리고 화면 중앙 탭, 카메라 버튼,
하단 스와이프 순서로 확인해 주세요. 처음에는 한 손가락으로 시험합니다.
펜·듀얼스크린·잠금/절전 전원 관리는 구현하지 않았습니다.

이 이미지가 재부팅되거나 화면이 달라졌다면 마지막 화면/경과 시간을 알려주세요.
문제 시 기존 `v60-xnu-boot2-gui.img`를 다시 임시 부팅해 비교합니다.
이번 작업은 기기에 플래시하거나 root 파일을 수정하지 않았습니다.
원본과 마찬가지로 XNU 실행 중 파일시스템 쓰기는 기존 설정을 따릅니다.

## 안 되면 로그용 이미지

```sh
fastboot boot v60-xnu-touch1-diag.img
```

동일한 터치 동작을 시도하되 커널 콘솔을 켭니다. `V60 TOUCH:` 줄을
촬영해 주세요. GUI가 콘솔 글자를 덮을 수 있으므로 영상이나 영구 로그가 유용합니다.

`v60-xnu-touch1-probe.img`는 SoC clock/GPIO/GENI를 설정한 뒤 칩 ID만 읽고
종료하는 비교용입니다. 이 모드는 컨트롤러의 동작 제어 레지스터를 쓰지 않지만,
SoC 레지스터·전원 GPIO 변경은 수행하므로 완전한 하드웨어 읽기 전용 검사는 아닙니다.

정상적인 초기화 로그 예시(아직 실기기에서 관찰한 결과가 아님):

```text
V60 TOUCH: SW42902 driver delay=60 mode=1
V60 TOUCH: SE13 fw=... status=... tx=... rx=...
V60 TOUCH: probe 0x28 rc=0 version=... irq=...
V60 TOUCH: polling 0x600, 10ms; finger events -> HID
V60 TOUCH: contacts=1 frames=...
```

다음 결과에 따라 추가 작업이 필요합니다:

- `need idle ABL-loaded I2C firmware`: 해당 GENI에 올바른 펌웨어가 없거나
  사용할 수 없는 상태. 이 버전은 펌웨어를 업로드하지 않고 중단합니다.
- `unsupported DFS0` / `clock halted`: 클럭 상태가 가정과 다름. 상태를 기록하고 중단합니다.
- `probe ... rc=...` 또는 identity 확인 실패: 버스/전원/프로토콜을 추가 진단해야 합니다.
- `contacts=...`는 뜨는데 UI 반응 없음: 이후 HID 서비스·좌표·사용자 공간 경로를 조사합니다.

기존 XNU 영구 로그가 활성화돼 있고 adb root가 가능한 기존 recovery로 부팅한 경우:

```sh
python3 v60-touch/pull_log.py --out v60-touch-log.txt
```

이 스크립트는 userdata 끝의 4MiB 로그 영역을 읽기만 합니다.
기대하는 로그 헤더가 없으면 중단하며, 파일시스템을 쓰거나 파티션을 플래시하지 않습니다.
recovery 이미지는 포함하지 않습니다.

## 변경 범위

- 성공한 이미지의 shim·DTB·ADT·CPU 수·메모리 배치를 보존.
- `_sweet_touch_init`부터 다음 모듈 전까지의 기존 코드 공간 안에 새 모듈 배치.
  전역 BSS/데이터 없이 worker 스택에 상태를 유지.
- 기존 커널의 thread/sleep/map/log/HID 함수 호출. Apple arm64의 printf 호출 규약은
  별도 어셈블리 브리지로 처리하고 테스트함.
- SW42902 칩 ID=10 및 protocol=4를 확인한 뒤에만 동작 제어 설정을 씀.
- GPIO 전원을 올리고 reset을 high로 놓음. reset-low, PMIC/RPMh 쓰기,
  펌웨어 업로드 및 flash/erase 명령은 구현하지 않음.
- 0x600 보고서를 10ms 간격의 IRQ 핀 폴링으로 읽고 최대 10개 손가락을 HID로 전달.
- 원본 HID start()는 2460인데 touch()의 Y clamp/edge는 2400인 불일치 확인.
  명령어 세 개만 2459/2411 기준으로 보정. 기존 2436행 논리 화면의 12행 crop도 반영.
- 부팅 옵션 `v60touch=1` 사용. `sweettouch=0`은 기존 모듈용 이름이므로 그대로 남음.

이 방식은 원본의 **고정 커널 가상 주소 배치**에 의존합니다.
다른 커널·ASLR 배치·다른 기기에는 적용하지 마세요.
빌더는 원본 SHA-256이 정확히 일치하지 않으면 거부합니다:

```text
6c916223cf86aca1caf13d4e490a20c519dfc7f315671bb494e643a2d31ab816
```

## 재빌드와 검증

Python 3, AArch64 GCC/binutils 필요. macOS에서는 `brew install aarch64-elf-gcc`.
Linux cross toolchain 사용 시 `CROSS_COMPILE=aarch64-linux-gnu-` 지정 가능.

```sh
python3 v60-touch/build.py /path/to/v60-xnu-boot2-gui.img --out work/touch-build
python3 v60-touch/test.py --out work/touch-tests
```

테스트는 호스트 C 컴파일러 clang과 QEMU도 사용합니다.

수행한 확인:

- SW42902 패킷 해석 15개 검사, AddressSanitizer/UndefinedBehaviorSanitizer 통과.
- Apple arm64 varargs stack slot 및 FP/LR 보존을 QEMU로 확인.
  Apple-target clang의 실제 호출 코드와도 stack layout을 대조.
- 모듈 크기 제한, 미해결 심볼/전역 BSS/데이터 없음 확인.
- boot image ID SHA-1 재계산, 원본과 shim/DTB/헤더 설정 동일 확인.
- 변경 바이트가 모듈 영역, 부팅 옵션, 지정 HID 명령어 세 개에만 있는지 확인.
- 전체 V60 부팅·실제 I2C/전원 응답·실제 터치/HID 동작은 검증 전.

최종 SHA-256 및 정확한 변경 주소는 함께 제공한 manifest.json을 확인하세요.
전체 XNU 원본/빌드 트리는 복구하지 못했으며, 향후 유지보수에는 대응 소스 확보가 필요합니다.
