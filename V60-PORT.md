# V60 포트 경로

현재 상태와 실행 방법은 [README.md](README.md), 실제 성공한 터치와 최신 수정은
[v60-touch/README.md](v60-touch/README.md)를 우선 참고합니다.

## 두 가지 빌드 경로

1. **현재 시험 이미지**: 정확한 boot2-gui 입력의 고정 모듈을 `v60-touch/build.py`로
   교체합니다. touch4는 사용자 실기기 터치 성공, touch5는 버튼/지연 개선 시험 대기입니다.
2. **기반 전체 XNU 빌드**: `xnu/patches/`, `tools/mkadt.py`, `tools/mkpayload.py`,
   `shim/`을 사용합니다. 대응하는 boot2 빌드 커밋/입력은 확인되지 않았으므로,
   이 경로로 동작한 이미지가 그대로 재현된다고 가정하지 않습니다.

`apply-v60-port.py`는 초기 upstream sweet 기반에 V60 상수를 적용하던 도구입니다.
현재 main에는 그 변경이 이미 있으므로 새 bring-up의 일괄 실행 단계로 취급하지 않습니다.
`SWEET`은 기반 XNU의 machine config와 심볼 이름이며 이름만 바꾸면 빌드가 깨질 수 있습니다.

V60에서는 화면1080×2460, 타이머27/30, restart146bf65c, SW42902가 핵심 차이입니다.
일반 payload 경로의 기존 apps watchdog 처리는 유지했습니다. CPU/메모리/ADT를 새로
구성하거나 다른 기기에 적용하려면 별도 검증이 필요합니다.

단계별 진단 이미지:

```sh
python3 tools/build-v60-diag.py --template /path/to/v60-shim-test1.img --out work/gic-read --mode gic-read
python3 tools/build-v60-diag.py --template /path/to/v60-shim-test1.img --out work/gic-write --mode gic-write
```

이 standalone 진단들은 XNU payload를 부팅하지 않습니다.
[검증 이력](docs/v60/HISTORY.md), [하드웨어 근거와 미확인 항목](docs/v60/HARDWARE.md)을 함께 봅니다.
