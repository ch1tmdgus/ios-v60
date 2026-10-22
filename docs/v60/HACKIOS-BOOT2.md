# V60 HackiOS boot2 — 2026-10-23

boot1의 진단 PID1 대기 코드를 고친 이미지입니다. **boot2 실기기 결과는 아직 없습니다.**

## 사진으로 확인한 boot1 결과

IMG_2225/2226에서 V60의 새 XNU8796.121.2, CPU0 등록, GICv3/타이머,
1080×2460 화면 출력, APFS `V60Bringup` 마운트와 `PID1 PASS`를 확인했습니다.
직후 `pid 1 exited`, exit reason namespace2/subcode0xa(SIGBUS),
`initproc failed to start` 패닉과 `MACH Reboot`가 나타났습니다.
첫 사용자 프로그램 진입은 성공했지만 지속 대기는 실패한 상태입니다.
코어 파일 error2는 진단 루트에 `/private/var/cores`가 없어 덤프를 저장하지 못한
후속 오류입니다. 사진만으로 SIGBUS의 정확한 PC/주소는 알 수 없습니다.

## 수정

- 원래 raw entry SP가 8 mod16일 수 있는데 정렬 없이 SP 기반 메모리 저장을 했습니다.
  QEMU의 기존 레지스터 덤프에서도 SP 끝자리가 `...e78`이었습니다. 시작 시
  SP를 16바이트 정렬합니다. 실제 SIGBUS가 SP alignment fault인지는 추가 예외 로그가 필요합니다.
- 기존 syscall240을 nanosleep으로 잘못 사용했습니다. 이 XNU에서는 `listxattr`입니다.
  이제 syscall93 `select(0,NULL,NULL,NULL,&timeout)`으로 1초 기다립니다.
  timeout은 정렬된 스택의 user64 timeval이고, carry/반환값을 검사합니다.
- 5회의 정상 대기 후 `WAIT PASS`를 출력하고 계속 반복합니다. `PID1 PASS` 한 줄만으로
  지속 실행을 통과시키던 테스트를 고쳐 heartbeat와 종료/패닉/예외 부재를 검사합니다.
- `systrace=1`을 켰습니다. 다시 예외가 나면 `usertrap`의 PC/SP/code를 사진에서 확인할 수 있습니다.
- 커널·shim·DTB·메모리 배치는 boot1과 같습니다. 바뀐 것은 자체 PID1, ADT의 빌드
  식별자와 예외 로그 옵션입니다. iOS GUI·입력·무선·GPU 기능은 여전히 켜지 않습니다.

QEMU UART 없는 1080×2460 화면 경로에서 실제 1초 timeout을 10회 지나
`WAIT PASS` 두 번, PID1 생존을 확인했습니다. 커널 진입 stub에서 SA0 설정도
요청하지만, 이것이 실제 폰과 같은 SP 예외 동작을 재현한다는 주장은 하지 않습니다.
실기기 SIGBUS 자체의 QEMU 재현에는 성공하지 않았습니다.

## 시험

```sh
fastboot boot v60-hackios-boot2-diag.img
```

`PID1 PASS` 뒤 아래 문구가 약 5초마다 추가되는지 **30초 이상** 확인해 주세요.

```text
V60 HACKIOS: WAIT PASS - 5 timed waits; PID1 alive (boot2)
```

반복 문구가 보이는 마지막 화면을 촬영합니다. 다시 멈추면 `usertrap`, `systrace`,
`panic`이 포함된 아래쪽 사진이 필요합니다. flash/erase/format은 하지 않습니다.
기존 데이터 파티션과 기존 touch5 이미지에는 변경을 가하지 않습니다.

빌드 방법/고정한 upstream 커밋과 shim 연결 구조는 [boot1 문서](HACKIOS-BOOT1.md)를
참고합니다. 현재 `hackios-port/build.py`는 boot2 이름으로 출력합니다.
