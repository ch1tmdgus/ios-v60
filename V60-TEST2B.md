# V60 test2b — 검은 화면 및 재부팅 구간 분리

## 확인된 상태

test1: 화면 출력과 UFS 읽기 성공.
IMG_2141/2142: test2 CPU 및 16KB MMU 활성화 화면 확인.
이후 사용자가 PAGE 2 뒤 검은 화면을 거쳐 재부팅됐다고 보고함.
PAGE 3 IRQ 결과는 관찰되지 않음. 따라서 IRQ 검사는 성공으로 판정하지 않음.

재부팅 시점만으로 GIC MMIO 접근 실패, GIC 설정 문제, IRQ 폭주,
watchdog 또는 MMU 활성화 이후 지연된 문제를 구별할 수 없음.
이전 test2는 이 구간의 변경이 연속으로 실행되어 관찰 지점이 부족했음.

## 먼저 실행할 이미지: v60-shim-test2b.img

```sh
fastboot boot v60-shim-test2b.img
```

임시 부팅. 16KB MMU를 켜고 GIC 상태를 **읽기만** 함.
GIC 레지스터/ICC 설정 쓰기, 타이머 설정, SGI 송신, IRQ 마스크 해제를 하지 않음.
UFS 접근이나 파티션 쓰기도 하지 않음. CPU_ON 실행도 없음.

3색 화면과 기존의 15초짜리 PAGE 1/2를 반복하지 않음.
첫 화면을 유지하며 다음 줄들을 차례로 덧붙임:

1. `STEP enable 16K MMU`
2. MMU rc 및 SCTLR
3. `STEP read ICC_SRE_EL1` → SRE
4. `STEP read GICD_CTLR / TYPER / IIDR` → GICD 값
5. `READ R base=...` → 해당 redistributor의 TYPER (일치할 때까지)
6. `STEP read GICR_WAKER / CTLR` → GICR 값
7. `STEP read PPI group / enable / pending` → PPI 값
8. `RESULT GIC register reads complete` → 화면 유지

STEP은 다음 동작 2초 전에, READ R은 1초 전에 표시함.
가능하면 부팅부터 짧은 영상으로 촬영하고, 마지막 화면을 전달해 주세요.
다시 검은 화면/재부팅이 발생하면 사라지기 직전 STEP/READ R 줄이 중요함.
파란 PANIC이면 STAGE, ESR, ELR, FAR를 포함해 촬영.

`GIC register reads complete`는 레지스터를 읽었다는 뜻이며 IRQ 전달 성공을
뜻하지 않음. 이 버전은 문제를 분리하는 진단 변경으로, 재부팅 원인 수정 완료를
의미하지 않음. 명시적인 재부팅 명령 없이 마지막 화면에서 멈춤.

## 비교용 이미지: v60-shim-test2b-mmu.img

test2b가 재부팅되는 경우, 이 이미지로 MMU만 유지했을 때도 같은지 비교:

```sh
fastboot boot v60-shim-test2b-mmu.img
```

16KB MMU와 캐시를 켠 뒤 **GIC 레지스터에는 접근하지 않음**.
5초마다 ALIVE seconds 값을 표시하고 120초 뒤 완료 화면에 멈춤.
숫자는 16진수: 0000001E=30초, 0000003C=60초, 00000078=120초.

- 비교용은 120초 완료, GIC 읽기용만 실패: GIC 접근 경로를 더 좁혀 검사.
- 비교용도 실패: MMU/캐시 활성화 이후 문제나 시간에 따른 재부팅도 계속 조사.
- 둘 다 완료: 다음 이미지에서 GIC 설정 쓰기와 IRQ 수신을 각각 분리해 검사.

어느 경우에도 이 두 이미지로 watchdog 등 원인을 단정하지 않음.

## 빌드

```sh
python3 tools/build-v60-diag.py --template /path/to/v60-shim-test1.img --out shim/out/test2b --mode gic-read
python3 tools/build-v60-diag.py --template /path/to/v60-shim-test1.img --out shim/out/test2b-mmu --mode mmu-hold
```

test1 DTB/빈 램디스크/boot-v2 설정 보존. 소스 묶음에는 개인 덤프가 없음.
기존 test2 이미지도 결과 비교를 위해 덮어쓰지 않고 유지함.
새 소스 패치는 upstream fdad0de8dc0e75c5ea84762adc03f7232dc91f19부터 누적;
기존 test1/test2 패치 위에 중복 적용하지 마세요.

## 로컬 검증

두 모드 GCC 빌드, C ABS64 relocation 검사, boot-v2 재분해 및 image ID 확인 통과.
test1 DTB와 ramdisk를 바이트 단위로 비교해 동일함을 확인.
GIC 읽기 모드 재빌드 결과 SHA-256 동일함을 확인.
실행 코드 디스어셈블에서 IRQ 마스크 해제와 ICC 설정 쓰기가 없음을 확인.

QEMU용 사본에서만 GIC 주소/PSCI 호출 방식을 virt 플랫폼 값으로 바꾸어 검증:
읽기 모드 `GIC register reads complete`, MMU 유지 모드 120초 완료까지 확인.
배포 이미지는 V60 GIC 주소/SMC를 유지. V60 실기기 test2b 검증은 아직 미진행.
