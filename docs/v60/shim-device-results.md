# V60 test2 실기기 결과

제공 사진: IMG_2141.HEIC, IMG_2142.HEIC.

- CPU: EL1 진입 확인.
- 카운터 주파수: 19.2 MHz.
- PSCI / SMCCC: 각각 1.1.
- CPU_ON: 지원 응답 확인. 보조 코어의 실제 기동은 아직 미검증.
- 16KB MMU: rc=0, L3 테이블 1개.
- SCTLR: 0xc00808 → 0xc0180d. MMU, 데이터 캐시, 명령어 캐시 활성화.
- shim 코드 / 프레임버퍼 / GIC 주소 변환: PAR bit0=0으로 성공.

현재 사진은 PAGE 1과 PAGE 2이며 PAGE 3 IRQ 결과는 포함되어 있지 않음.
최종 VTIMER/PTIMER/SGI 카운터 또는 PANIC/정지 여부를 확인한 뒤
실기기 인터럽트 성공 여부를 판정해야 함.

추가 사용자 보고: PAGE 2 뒤 잠시 검은 화면이 보이고 재부팅됨.
IRQ 결과가 나오지 않았으므로 IRQ 성공으로 판정하지 않음.
MMU 활성화의 순간적인 성공과 장시간 안정성을 구분해야 함.
다음 검사는 test2b GIC 읽기 및 MMU 유지 비교 이미지로 진행.

IMG_2145.HEIC: test2b에서 MMU 활성화 뒤 GIC register reads complete 및 END 확인.
설정 쓰기 및 IRQ 수신은 아직 미검증. 다음은 test2c 설정 쓰기 분리 검사.
