# V60 영구 로그 수집

최신 문제 진단은 `v60-xnu-touch5-inspect.img`로 합니다. GUI 표시 후에는
화면 로그를 숨기지만 Preferences IOKit trace와 프로세스 stack은 영구 로그에 남습니다.

```sh
fastboot boot v60-xnu-touch5-inspect.img
```

부팅 후60초 이상 기다려 터치를 확인하고 버튼/스크롤을 시험합니다.
설정 → 일반 → 정보를 눌러 멈춘 상태를 약2분 유지한 뒤 기존 adb-root recovery로
돌아옵니다. inspect sampler는 약90초 뒤 첫 회, 이후120초 간격입니다.

저장소에서 실행:

```sh
python3 v60-touch/pull_log.py --out v60-touch5-log.txt
```

Release bundle에서 실행:

```sh
python3 v60-pull-touch-log.py --out v60-touch5-log.txt
```

도구는 userdata 크기를 읽고 마지막1025개의4KiB 블록에서 기존 SWEETLOG v1
헤더/4MiB 링을 검증합니다. 파티션을 쓰거나 mount/format/flash하지 않습니다.
기존 recovery와 adb root가 필요하며 XNU 실행 중 `sweetusb=0`에서는 adb를 기대하지 않습니다.

터치/버튼/Preferences sample 일부는 터미널에 요약되고, 분석용 전체 내용은 지정한
텍스트 파일에 있습니다. 요약에 원인이 안 보여도 전체 파일을 확인해야 합니다.
터치가 동작한 touch4에도 같은 추출기를 사용할 수 있습니다.

로그와 기기 덤프는 소스 Git/Release에 포함하지 않습니다.
