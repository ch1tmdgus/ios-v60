# iOS 16 on LG V60

LG V60 ThinQ 5G (`timelm`, Qualcomm SM8250 / Snapdragon 865)용 실험적인
**iOS 16 / XNU 포팅 프로젝트**입니다. 기존 `ios-sweet` 기반 작업을 V60으로 옮깁니다.

**2026-10-19: V60에서 iOS GUI 부팅과 touch4의 실제 터치 동작이 확인됐습니다.**
최신 touch5는 버튼 매핑·입력 지연·화면 로그 개선 버전이며, 해당 변경의 실기기
결과는 아직 대기 중입니다. 검은 배경화면과 설정 → 일반 → 정보 진입 시 멈춤은 미해결입니다.

## 다운로드

[실험 빌드와 체크섬 — Releases](https://github.com/ch1tmdgus/ios-v60/releases/tag/v60-bringup-20261019)

| 목적 | 파일 | 확인 상태 |
| --- | --- | --- |
| 실제 터치가 동작한 버전 | `v60-xnu-touch4-gui.img` 또는 `v60-touch4-bundle.zip` | 사용자 실기기 성공 보고 |
| 최신 기본 GUI | `v60-xnu-touch5-gui.img` | 빌드·호스트 검사 통과, 실기기 대기 |
| 최신 문제 진단, 화면 로그 숨김 | `v60-xnu-touch5-inspect.img` | 설정 정보 멈춤/버튼 진단용 |
| CPU 부하 비교 | `v60-xnu-touch5-balanced.img` | touch5 입력 개선 + 화면 복사 목표30Hz |
| 과거 shim/터치 단계 | `v60-shim-test*-bundle.zip`, `v60-touch1..3-bundle.zip` | 아래 기록 참고 |

각 bundle에는 해당 버전 이미지·대체 모듈 소스·설명서·체크섬이 있습니다.
Release의 `SHA256SUMS.txt`로 다운로드를 확인할 수 있습니다.
Apple IPSW, iOS 루트 이미지, 사용자 파티션/캘리브레이션 덤프는 배포하지 않습니다.
이 부팅 이미지만으로 iOS 사용자 공간이 설치되는 것은 아닙니다.

## 현재 동작 상태

| 항목 | V60 상태 |
| --- | --- |
| 부팅 | 제공된 `v60-xnu-boot2-gui.img`로 XNU/iOS GUI 부팅을 사용자 확인 |
| 터치 | **touch4 성공**: SW42902, GPIO 소프트웨어 I²C → 기존 HID 경로 |
| 터치 통신 | GENI SE13의 FIFO 비활성/GSI-only 확인. GSI DMA 이식 대신 GPIO36/37 사용 |
| touch5 입력 개선 | 한 손가락 보고서156→48바이트, 추가 접점만 이어 읽기, 폴링10→2ms. 실기기 대기 |
| 전원·볼륨 버튼 | touch5에서 기존 SPMI observer 드라이버 활성화, V60 볼륨업 매핑 적용. 실기기 대기 |
| 화면 로그 | touch5는 GUI 표시 후 숨김. 영구 로그는 유지하며 panic은 표시 가능 |
| 프레임/반응 | CPU framebuffer 복사. touch5 기본 목표60Hz, 비교용30Hz. 실제 fps 미측정, GPU 가속 미구현 |
| 배경화면 | 검은색으로 남는 문제 미해결 |
| 설정 → 일반 → 정보 | 누르면 멈추거나 반응하지 않는 증상. 원인 미확인 |
| CPU/SMP | 성공한 이미지의 `cpus=4`와 ADT 유지. 코어별 동작/8코어는 별도 미검증 |
| UFS/루트 | shim의 read-only UFS/GPT 확인 및 이후 사용자 공간 부팅 보고. 일반 설치 절차는 제공하지 않음 |
| 배터리 | 아이콘 표시가 실제 연료계 측정의 증거는 아님. V60 하드웨어 연동 미검증 |
| USB/네트워크·오디오·카메라·GPU | V60용으로 검증된 지원 없음 |

화면의 카메라/배터리 아이콘은 해당 하드웨어가 동작한다는 뜻이 아닙니다.
이전 Redmi에서의 성공 기록은 V60 결과와 구분합니다.

## 시험 방법

동작했던 원본/touch4 이미지를 보관하고 잠금 해제된 V60에서 **임시 부팅**합니다.
현재 진단 이미지는 다음과 같습니다:

```sh
fastboot boot v60-xnu-touch5-inspect.img
```

1. 부팅 후60초 이상 기다립니다. 이후 터치 worker가 시작합니다.
2. 화면 중앙 탭·하단 스와이프·스크롤, 볼륨 두 버튼과 전원 짧게 누르기를 시험합니다.
3. 설정 → 일반 → 정보가 멈춘 상태로 약2분 유지합니다.
4. 기존 adb root recovery로 돌아와 로그를 읽습니다:

```sh
python3 v60-touch/pull_log.py --out v60-touch5-log.txt
```

Release bundle의 `v60-pull-touch-log.py`도 같은 도구입니다. **저장된 전체 로그**로
설정/배경화면 문제를 분석합니다. 추출기는 userdata 끝의 기존4MiB 로그를 읽기만 합니다.
XNU 실행 중에는 `sweetusb=0`이므로 adb 연결을 기대하지 않습니다.

GUI 표시 뒤에는 inspect도 화면에 로그를 띄우지 않습니다. 최초 부팅 글자는 잠시
보일 수 있습니다. inspect의 stack sample은 약90초 뒤, 이후120초 간격으로 기록됩니다.
일반 사용 비교는 `touch5-gui`, 더 버벅이면 `touch5-balanced`로 합니다.

기존 이미지의 파일시스템 쓰기 옵션은 그대로입니다. 임시 부팅이라고 iOS 루트가
읽기 전용인 것은 아닙니다. 이 저장소의 빌더/로그 추출기는 파티션을 플래시하지 않습니다.

## 어떻게 만든 이미지인가

원본 GUI 부팅 이미지의 심볼과 고정 가상 주소를 확인하여, 비활성 터치 모듈 공간을
`v60-touch/`의 독립 코드로 교체합니다. HID 좌표 상수3개와 touch5 버튼 상수2개,
진단 문자열3개도 원래 값을 검증한 뒤 바꿉니다. shim·DTB·ADT·메모리·CPU 배치는 보존합니다.

**성공한 boot2 이미지와 일치하는 전체 XNU 빌드 소스/커밋은 아직 확인되지 않았습니다.**
이 저장소에는 기반 XNU 패치와 V60 수정이 있지만, 전체 커널을 재빌드하여 동일한
boot2 이미지를 재현했다고 주장하지 않습니다. 최신 시험 이미지는 아래 **정확한 원본**이 필요합니다:

```text
v60-xnu-boot2-gui.img
SHA-256 6c916223cf86aca1caf13d4e490a20c519dfc7f315671bb494e643a2d31ab816
Darwin 22.6.0 / xnu-8796.141.3
```

빌더는 다른 입력(touch1~5 결과 이미지 포함)을 거부합니다. 다른 커널/슬라이드/기기에
이 고정 주소 모듈을 적용하지 않습니다. 소스만으로 원본 전체 커널이 자동 생성되지는 않습니다.

## 터치 이미지 빌드

Python3, AArch64 GCC/binutils가 필요합니다. 호스트 검사는 clang과 QEMU도 사용합니다.

```sh
# macOS
brew install aarch64-elf-gcc qemu

python3 v60-touch/test.py --out work/touch-tests
python3 v60-touch/build.py /path/to/v60-xnu-boot2-gui.img --out work/touch5
```

Linux에서는 `CROSS_COMPILE=aarch64-linux-gnu-`를 지정할 수 있습니다.
버전별 구현은 Git 이력과 [소스 버전 표](docs/v60/HISTORY.md)에 남겨 두었습니다.
최신 코드/시험 절차는 [v60-touch/README.md](v60-touch/README.md)를 참고하세요.

검사는 ASan/UBSan 보고서 해석·길이 검사, 독립 I²C 모의 슬레이브, QEMU printf ABI,
모듈 크기/심볼/패치 범위, boot image ID, 체크섬을 포함합니다.
호스트/QEMU 통과와 V60 실기기 성공은 서로 다른 결과입니다.

기반 XNU 전체 빌드 경로는 [xnu/README.md](xnu/README.md), 초기 V60 이식 도구는
[V60-PORT.md](V60-PORT.md)를 참고하세요. `SWEET` 머신 이름과 기존 심볼은 기반
코드의 빌드 식별자이므로 단순히 `V60`으로 치환하지 않습니다.

## 이력과 하드웨어 자료

- [shim test1~2c, touch1~5 진행/검증 기록](docs/v60/HISTORY.md)
- [V60 하드웨어 주소·현재 미확인 항목](docs/v60/HARDWARE.md)
- [shim 초기 bring-up](V60-BRINGUP.md), [test2b](V60-TEST2B.md), [test2c](V60-TEST2C.md)
- [touch4 실험 설명](docs/v60/TOUCH4.md), [touch5 설명](v60-touch/README.md)
- [버전별 manifest / SHA-256](docs/v60/releases/)

## 저장소 구성

| 경로 | 내용 |
| --- | --- |
| `v60-touch/` | 최신 SW42902/GPIO I²C 모듈, 고정 이미지 빌더, 호스트/QEMU 검사, 로그 추출기 |
| `shim/` | V60 기본 포트와 단계별 CPU/MMU/GIC 진단 코드 |
| `tools/build-v60-diag.py` | 제공된 test1을 템플릿으로 진단 이미지 생성 |
| `tools/` | ADT/payload/Android boot 이미지 및 기존 개발 도구 |
| `xnu/patches/` | 기반 XNU 패치, 기존 V60 변경. 전체 빌드의 실기기 재현은 별도 |
| `patches/v60/` | 최초 제공된 V60 shim 패치 |
| `docs/v60/` | 관찰 결과·버전 이력·하드웨어 설명·manifest |
| `NOTES.md` | 현재 V60 인계 및 분리 표시된 과거 upstream 기록 |

원시 DT/파티션 덤프·시리얼·개인 로그·Apple 사용자 공간은 커밋하지 않습니다.
잘못된 이름으로 올라와 있던 `live.dts`는 실제 DT가 아닌 `apply-v60-port.py`의
동일 복사본이어서 제거했습니다. 실제 장치 트리 덤프는 빌드 소스에 포함하지 않습니다.

## 기반 프로젝트와 출처

- [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet) — XNU/부팅·디스플레이·HID 기반
- [MomoYuuki/android_kernel_lge_sm8250](https://github.com/MomoYuuki/android_kernel_lge_sm8250)
- [rofikkerneldev/kernel_lgv600tm_4.19](https://github.com/rofikkerneldev/kernel_lgv600tm_4.19) — 사용자 Android 커널 빌드 자료, SW42902/GPIO/클럭/SPMI 대조
- [Apple XNU](https://github.com/apple-oss-distributions/xnu)
- [blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build)

대체 터치 모듈은 GPL-2.0-only이며 [라이선스](v60-touch/COPYING)를 포함합니다.
기반 코드와 타사 구성 요소의 개별 라이선스/저작권 표시는 유지합니다.
