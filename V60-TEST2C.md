# V60 test2c — GIC 쓰기와 IRQ 수신 분리

## test2b 실기기 결과

IMG_2145.HEIC에서 `GIC register reads complete` 및 END 확인.
16KB MMU 활성화 뒤 GIC 상태를 읽는 진단 경로 완료.

사진에서 판독한 값:

```text
ICC_SRE_EL1      0x1
GICD_CTLR        0x10
GICD_TYPER       0x007b041d
GICD_IIDR        0x0201143b
GICR base        0x17a60000
GICR_TYPER       0x21
GICR_WAKER       0x0
GICR_CTLR        0x0
PPI IGROUPR0     0x0
PPI ISENABLER0   0x0
PPI ISPENDR0     0x08840000
PPI ISACTIVER0   0x0
```

레지스터 읽기 성공은 설정 쓰기 권한이나 IRQ 전달 성공을 보장하지 않음.
MMU-only 120초 검사의 실기기 결과는 제공되지 않았음.
기존 test2의 재부팅 원인은 아직 미확정.

## 실행

```sh
fastboot boot v60-shim-test2c.img
```

임시 부팅이며 파티션을 쓰지 않음. UFS 및 XNU payload 경로를 실행하지 않음.
**CPU IRQ 마스크를 끝까지 유지**하고 타이머 설정/SGI 송신도 하지 않음.
GIC 제어 레지스터와 CPU 인터페이스 설정을 단계적으로 변경함.

처음부터 짧은 영상으로 촬영해 주세요. 매 STEP은 실제 쓰기 2초 전에 표시되고,
쓰기 뒤에는 `DSB/ISB`와 읽기값 표시가 따라옴.

진행 순서:

1. MMU 활성화.
2. GICD_CTLR에 기존 test2와 같은 `old | 0x13` 쓰기, RWP 최대 1초 대기.
3. boot CPU redistributor 확인. WAKER가 깨어 있는 상태가 아니면 STOP.
4. IGROUPR0에서 SGI1/VTIMER27/PTIMER30 비트만 설정 시도.
5. 세 ID의 priority를 하나씩 쓰고 읽기값 표시.
6. 해당 세 ID의 enable 비트 쓰기 및 읽기값 표시.
7. ICC_CTLR / PMR / BPR1 / IGRPEN1을 각각 쓰고 읽기값 표시.
8. 마지막 `write sequence complete / IRQ masked` 화면 유지.

기존 test2와 달리 전체 32개 PPI/SGI priority를 변경하지 않고 세 ID만 다룸.
IGROUPR0도 원래 값을 보존하며 세 비트만 OR함. 이 최소화가 문제에 영향을
줄 가능성이 있으므로, 완료되어도 기존 재부팅의 원인을 바로 단정하지 않음.

최종 화면을 보내 주세요. 다시 재부팅된다면 마지막 STEP이 보이는 영상/사진,
파란 PANIC이면 STAGE/ESR/ELR/FAR를 포함한 화면이 필요함.

최종 문구는 쓰기 명령과 읽기 과정이 끝났다는 뜻임.
쓰기값과 읽기값이 다를 수 있어 읽기 결과를 검토해야 하며,
이 버전은 IRQ 수신 성공 여부를 검사하지 않음.

## 빌드

```sh
python3 tools/build-v60-diag.py --template /path/to/v60-shim-test1.img --out shim/out/test2c --mode gic-write
```

제공 패치는 upstream fdad0de8dc0e75c5ea84762adc03f7232dc91f19부터 누적.
기존 test1/test2/test2b 패치 위에 중복 적용하지 마세요.
기기 DTB와 개인 덤프는 소스 묶음에 넣지 않음.

## 로컬 확인

GCC 빌드 및 ABS64 relocation 검사 통과.
boot-v2 재분해, image ID SHA-1, Linux Image 크기 확인 통과.
test1과 DTB/빈 램디스크 바이트 단위 동일 확인.
디스어셈블에서 IRQ 마스크 해제, 타이머 enable, SGI 송신이 없음을 확인.

QEMU 검증용 사본은 GIC 주소 및 PSCI 방식만 플랫폼에 맞게 바꾸고,
V60 사진의 WAKER=0 상태를 모사하기 위해 시작 시 QEMU GICR을 깨움.
이 사본에서 `write sequence complete / IRQ masked`와 마지막 DAIF=0x3c0 확인.
이 QEMU 전용 wake 코드는 배포 이미지/소스에는 포함하지 않음.
실기기 test2c는 아직 미검증.
