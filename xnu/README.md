# V60의 기반 XNU 빌드

`.github/workflows/xnu.yml`은 pinned blacktop/darwin-xnu-build와 `xnu/patches/`를
이용하는 기존 전체 빌드 경로입니다. 머신 설정 이름은 `SWEET`을 유지합니다.
V60용 framebuffer/timer/ADT 기반 변경이 포함되어 있습니다.

**현재 동작한 boot2-gui 이미지와 일치하는 전체 빌드 커밋은 확인되지 않았습니다.**
이 workflow를 성공시켰다고 touch4/5의 고정 이미지나 실기기 동작이 재현되는 것은 아닙니다.
현재 시험 이미지를 만들려면 [v60-touch 빌더](../v60-touch/README.md)를 사용합니다.

기존 workflow는 macOS/Xcode의 특정 버전과 API에 의존합니다. 이 공개 정리에서는
전체 XNU 재빌드/실기기 검증을 새로 수행했다고 주장하지 않습니다. 실행이 필요하면:

```sh
gh workflow run xnu.yml -R ch1tmdgus/ios-v60
gh run list -R ch1tmdgus/ios-v60 --workflow xnu.yml
# 완료된 run과 실제 artifact 이름을 확인한 뒤 다운로드합니다.
gh run download RUN_ID -R ch1tmdgus/ios-v60 -n xnu-kernel -D xnu/out
```

호스트 터치 검사는 `.github/workflows/v60-touch.yml`에서 별도로 실행합니다.
기반 패치의 upstream 참고 이름과 저작권/라이선스 표시는 유지합니다.
