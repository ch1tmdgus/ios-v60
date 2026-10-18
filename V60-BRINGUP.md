# LG V60 bring-up — 2026-10-19

현재 산출물은 **진단용 shim**입니다. XNU/iOS 부팅 이미지는 아직 아닙니다.
사용자가 성공시킨 test1을 출발점으로 CPU·16KB MMU·타이머 IRQ를 확인합니다.

## test1에서 확인된 사실

사용자 사진 IMG_2139.HEIC에서:

- 1080×2460 프레임버퍼의 문자 출력과 3색 전환 성공.
- UFSHCI version 0x300, HCE=1, HCS=0x10f.
- TUR / READ CAPACITY / READ 모두 rc=0, OCS/status=0.
- LUN0 블록 크기 0x1000 (4096), 마지막 LBA 0x01d6c7ff.
- protective MBR 0xaa55, GPT `EFI PART`, 0x20개 엔트리.
- userdata 첫 LBA **0x005e2e28**, Android 이름 연결은 `/dev/block/sda28`.
- `no SWEETXNU payload`는 빈 램디스크를 넣은 test1의 정상 결과.
- `fs other`는 원래 검사기가 f2fs/ext4만 구별해서 나온 결과로,
  손상 또는 HFS+의 증거가 아님.

주소/IRQ는 dt.tar의 실제 `base/`와 Rofik 커널 kona.dtsi에서 대조:
GICD 0x17a00000, GICR 0x17a60000 / 0x100000, stride 0x20000,
UFS 0x01d84000, virtual timer INTID 27, physical timer INTID 30,
PSCI method `smc`, counter frequency 19.2 MHz.
IMEM restart reason 0x146bf65c는 test1 변경을 유지하지만 진단 정상 종료에서는 쓰지 않음.

`v60.zip/live.dts`는 잘못 생성되어 Finder .DS_Store만 포함하므로 사용하지 않음.
`v60.zip/dmesg.txt`는 0바이트. dt.tar의 원본 속성은 유효함.

## 먼저 실행할 이미지

```sh
fastboot boot v60-shim-test2.img
```

임시 부팅이며 파티션을 플래시하지 않습니다. 이 진단 경로는 UFS에 접근하거나
XNU payload를 실행하지 않습니다. 화면은 빨간 바탕이며 3색 테스트는 반복하지 않습니다.

1. PAGE 1: CPU/PSCI/기능 레지스터, 15초.
2. PAGE 2: 16KB MMU, SCTLR/TCR/PAR, 15초. 이 화면도 촬영해 주세요.
3. PAGE 3: GICv3, VTIMER(27), PTIMER(30), SGI1.
   마지막 `RESULT`와 카운터가 계속 남습니다. 이 화면을 촬영해 주세요.

정상 예상: MMU rc=0, SCTLR의 M/C/I 비트가 켜짐, PAR의 bit0=0.
두 timer의 irqs가 0보다 크고 SGI1 irqs=0xA이면 RESULT PASS.
정확한 timer 횟수는 실행 속도에 따라 달라집니다. PASS는 이 진단 범위만 의미합니다.
파란 PANIC이면 STAGE/ESR/ELR/FAR 화면을 보내 주세요.
마지막 화면에서는 의도적으로 멈춥니다. 전원/볼륨 키를 이용해 기존 방식으로 재부팅합니다.
펌웨어 watchdog 등이 켜져 있으면 기기가 자체 재부팅할 가능성은 남아 있습니다.

## 보조 SMP 이미지

기본 검사 결과를 먼저 확인한 다음 선택적으로:

```sh
fastboot boot v60-shim-test2-smp.img
```

MMU/cache를 켜지 않고 보조 코어를 하나씩 PSCI CPU_ON 합니다.
CPU별 ret, MIDR, MPIDR을 표시하고 CPU_OFF를 요청합니다.
boot CPU는 건너뛰며 보조 코어의 GIC 레지스터를 변경하지 않습니다.
`ret=0` + MPIDR bit63=1은 보조 코어 진입 보고 성공을 뜻합니다.
bit63은 보고 완료 표시이고 실제 MPIDR의 일부가 아닙니다.
CPU_OFF 완료나 XNU SMP 동작까지 보증하지는 않습니다.
이 이미지는 실제 V60에서 아직 실행하지 않았습니다.

## 재빌드

upstream fdad0de8dc0e75c5ea84762adc03f7232dc91f19 기준입니다.
제공된 `v60-shim-test2.patch`에는 test1 변경도 포함합니다.
깨끗한 upstream checkout에서 `git apply v60-shim-test2.patch` 하거나
제공 소스 묶음을 사용합니다. 이미 test1을 적용한 트리에 패치를 중복 적용하지 마세요.

Python 3와 AArch64 GCC/binutils 필요:
macOS `brew install aarch64-elf-gcc`, Linux는 aarch64-linux-gnu 도구 사용 가능.

```sh
python3 tools/build-v60-diag.py --template /path/to/v60-shim-test1.img --out shim/out/v60-test2 --mode cpu
python3 tools/build-v60-diag.py --template /path/to/v60-shim-test1.img --out shim/out/v60-test2-smp --mode smp
```

기본 `shim/build.sh` 대신 위 전용 빌드 도구를 사용합니다.
전용 도구는 test1의 DTB, 빈 cpio ramdisk, boot-v2 헤더 설정을 그대로 재사용하고
새 kernel 크기와 Android image ID를 계산합니다. C 객체 ABS64 relocation을 거부합니다.
DTB/파티션 덤프/사진은 소스에 포함하거나 git에 커밋하지 않았습니다.

## 다음 XNU 단계의 조건

- test2 결과로 V60 16KB MMU와 IRQ 경로를 실기기에서 확인.
- V60용 ADT의 timer INTID/화면/메모리 범위를 지정하고 cpus=1로 초기 XNU 진입.
- sweet의 `--highmem` 값을 그대로 사용하면 안 됨: V60 memory/reg는
  [0x80000000,0xbbb00000), [0xc0000000,0x180000000),
  [0x180000000,0x280000000)이며 여러 예약 영역이 있음.
  RAM 노드만으로 커널에 넘길 연속 영역을 확정하지 않음.
- 초기에는 sweet의 touch/SPMI/RPMh/USB 드라이버를 끄거나 V60 값으로 이식.
  같은 UFS/GIC 주소가 전체 주변장치 호환을 뜻하지 않음.
- userdata의 읽기 성공은 XNU 쓰기/파일시스템 검증과 별개.
  현재 사용자 요청에는 userdata 포맷/덮어쓰기 승인이 없음.
  upstream NOTES의 다른 사용자가 한 과거 승인을 이 V60에 적용하지 않음.

## 기준 소스

- ios-sweet fdad0de8dc0e75c5ea84762adc03f7232dc91f19
- rofikkerneldev/kernel_lgv600tm_4.19 7eab1a7189a7aba0a0c4a30ba616619e097ced52
- MomoYuuki/android_kernel_lge_sm8250 51a0d09fb65ead68b83d2533eea22d2f866b9ea0

소스 검토 및 로컬 빌드 결과는 실기기 검증과 구분합니다.

## 로컬 검증 결과

- 두 이미지 AArch64 GCC 16.2.0 빌드 성공, C ABS64 relocation 없음.
- 각각 재분해해 kernel image_size, boot 헤더, image ID SHA-1 확인.
- test1과 DTB/ramdisk 바이트 단위 동일 확인.
- QEMU virt/Cortex-A76/GICv3 smoke test에서 16KB MMU 후 최종 PASS:
  VTIMER=25, PTIMER=25, SGI1=10, other=0.
  에뮬레이터용 사본에서만 GIC 주소와 PSCI SMC→HVC를 바꿨으며,
  배포 V60 이미지에는 V60 GIC 주소와 SMC를 유지함.
- V60 실기기 test2 및 SMP 검증은 아직 진행 전.

이미지 SHA-256:

```text
7efa4058aaf0cb27edcf18e8fc7e7a2ed3787c182b327c640163b0504c39cd14  v60-shim-test2.img
b2e019e9b2f7c80c0ea95da93103083eb477087e34b58f6b5521a35adfa5fa94  v60-shim-test2-smp.img
```
