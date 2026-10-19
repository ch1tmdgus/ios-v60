# V60 bring-up / 터치 버전 이력

확인 기준일: 2026-10-19. 실기기 결과는 사용자 사진/로그/동작 보고이며,
호스트 검사와 구분합니다. 최신 소스는 `v60-touch/`, 시험 이미지는 Releases에 있습니다.

## Shim 단계

| 단계 | 실기기 관찰 | 소스 |
| --- | --- | --- |
| test1 | 3색 전환, UFS TUR/CAP/READ rc0, 4096-byte 블록, GPT 확인. XNU payload 없음 | `patches/v60/v60-shim-test1.patch` (제공 원본) |
| test2 | EL1, 19.2MHz, PSCI/SMCCC1.1. 16KiB MMU 활성화 확인 뒤 검은 화면/재부팅. IRQ 결과 없음 | tag `v60-shim-test2`, commit `7ce88ac` |
| test2b | MMU 뒤 GIC register reads complete. IRQ masked | tag `v60-shim-test2b`, commit `6253622` |
| test2b-mmu | MMU-only120초 대조 이미지, 실기기 결과 대기 | test2b와 동일 |
| test2c | 단계별 GIC 쓰기 순서 완료. IRQ masked이며 실제 전달 미검증 | tag `v60-shim-test2c`, commit `6a55262` |
| test2-smp | 별도 secondary-core 진단, 실기기 결과 대기 | test2와 동일 |

기존 XNU/GUI 성공이 이전 standalone IRQ 테스트의 성공을 의미하지는 않습니다.
재부팅 원인도 특정 레지스터/워치독으로 단정하지 않습니다.

## GUI / 터치 단계

| 버전 | 변경/관찰 | 정확한 소스 commit |
| --- | --- | --- |
| boot2-gui | 사용자 제공 이미지로 시계·배터리·홈바·카메라 버튼 표시, 터치 불가 | 대응하는 전체 빌드 commit 미확인 |
| touch1 | SW42902 대체 모듈 첫 구현. 실기기에서 clock18008 value80000002로 조기 중단 | `f6238d4` (touch1 코드 + GPL 문구) |
| touch2 | vendor HWCG 예외 반영. 클럭 통과, SE13 fw303/width32 확인, engine 검사에서 중단 | `0bce09b` |
| touch3 | FIFO_IF_DISABLE=1, reason2 실기기 확인. GSI-only이므로 GPIO/칩 명령 전 종료 | `0199750` |
| touch4 | GPIO36/37 소프트웨어 I²C, ID 검증 뒤 보고서/HID 연결. **사용자 터치 성공 보고** | `a55639d` |
| touch5 | vendor flexible report, poll2ms, PMIC 버튼 매핑, quiet GUI,60/30Hz 프로필. **실기기 미검증** | `62fb34c` |

각 commit에 `v60-touch1` … `v60-touch5` tag가 연결됩니다. 과거 상태를 살펴보려면:

```sh
git show v60-touch4:v60-touch/v60_touch.c
git switch --detach v60-touch4
```

Release의 버전별 source ZIP은 당시 `v60-touch/` 소스입니다. shim source ZIP도
당시 전체 소스 snapshot이며 최신 통합 소스와 바이트 단위로 같다고 가정하지 않습니다.

## 실제 중단 로그 핵심

```text
# touch1
clock halted off=18008 value=80000002
# touch2
SE13 fw=303 status=0 tx=20102864 rx=20204800
# touch3
FIFO_IF_DISABLE=1 tx_width=32 tx_depth=16 rx_width=32 rx_depth=32
engine blocked reason=2
```

실제 터치 성공은 touch4에 대한 사용자 보고입니다. touch5의 버튼 성공·60fps·
배경화면 수정·정보 화면 수정은 아직 확인하지 않았습니다. 개인 로그/사진은 포함하지 않습니다.

## 공개 소스 통합

기존 `ios-v60` main `7130b4c` 위에 로컬 작업 이력을 병합했습니다.
기존 V60 watchdog/ADT/XNU 패치 변경은 유지했습니다. `shim_main` 일반 payload
경로의 watchdog 처리를 보존하고, 진단 경로는 그 앞의 분리된 `V60_DIAG`를 사용합니다.
색상 테스트를 일반 payload 부팅에 무조건 추가하지 않았습니다.
원래 작성한 단계별 소스는 병합 부모와 위 tag에서 확인할 수 있습니다.
