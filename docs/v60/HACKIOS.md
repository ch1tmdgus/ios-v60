# HackiOS 새 소스의 V60 적용 검토 — 2026-10-22

결론: **V60 이식의 새 기반으로 사용할 가치가 크며 기술적으로 가능성이 있습니다.**
V60에서 GPU·Wi-Fi가 동작하는 상태로 이식된 것은 아닙니다. 아래는 두 저장소의
실제 소스와 V60 참조 커널을 대조한 초기 검토이며, 이후 [boot1 진단 이미지](HACKIOS-BOOT1.md)를
생성해 QEMU의 APFS/PID1/화면 출력을 검증했습니다. 이후 V60 사진에서도 같은 단계에
도달했지만 PID1이 SIGBUS로 종료됐습니다. [boot2 수정판](HACKIOS-BOOT2.md)의 실기기 확인을 기다립니다.

## 확인한 소스

- [kernel](https://github.com/oosexgang/hackios-xnu-kernel/tree/8e61187098f2c26c5daee8f9f72b6416aa64b9ab)
  `8e61187098f2c26c5daee8f9f72b6416aa64b9ab`
- [userland](https://github.com/oosexgang/hackios-xnu-userland/tree/906425b75e02b4f99eb5d7bb1ef7c9583472d772)
  `906425b75e02b4f99eb5d7bb1ef7c9583472d772`
- V60 참조 커널 Rofik `7eab1a7189a7aba0a0c4a30ba616619e097ced52`
- 구조화된 비교: [v60-assessment.json](../../hackios-port/v60-assessment.json)

원본을 작업 폴더 `work/hackios-xnu-{kernel,userland}-reference`에 각각 받아 확인했습니다.
외부 README는 분석 자료로 취급했습니다. 이후 사용자의 이미지 제작 요청에 따라 검토한
`xnu/apply.sh`로 별도 소스 트리를 준비하고 APFS 빌더를 실행했습니다.

## 실제로 재사용할 기반

`xnu/files`의 IOSurface/프레임버퍼·GPU 서비스, APFS/사용자 공간 지원,
`wifi/lx`의 LinuxKPI 연결, ath11k 데이터 경로와 iOS 네트워크 연결이 존재합니다.
사용자 공간에는 `userland/src/eagl.c`, Mesa/freedreno의 Darwin 연결부와
Backboard/SpringBoard 주입 라이브러리가 있습니다. 이는 기존 고정 주소 바이너리
수정 작업에서 직접 구현해야 했던 많은 기반을 제공합니다.

새 프로젝트는 XNU8796.121.2와 iOS16.5 조합입니다. 현재 성공한 V60 이미지는
XNU8796.141.3 기반입니다. 기존 모듈의 고정 가상 주소/바이너리 패치를 새 커널에
적용하지 말고, 맞는 커널·사용자 공간을 소스로 빌드해야 합니다.

## 하드웨어 차이

| 항목 | 공개 소스의 기준 | V60에서 해야 할 일 |
| --- | --- | --- |
| SoC | POCO X5 Pro / SM7325 | SM8250 플랫폼·전원·인터럽트·메모리 맵 적용 |
| GPU | Adreno642L / chip0x06030500 | Adreno650 / 참조 DT chip0x06050000; GMU·SMMU·전원·펌웨어 검증 |
| GMU 주소 | 0x03d69000 | V60 참조 DT0x03d6a000; 같은 주소라고 가정하지 않음 |
| GPU 내부 메모리 | 코드가512KiB와 bank bit14를 반환 | A650 참조 코드1152KiB와 DT bank bit16; ioctl/메모리 설정까지 변경 |
| GPU 펌웨어 | a660 계열 파일을 사용하는 구현 | V60의 a650_gmu.bin/a650_sqe.fw/a650_zap 계열을 실물에서 확인 |
| Wi-Fi | WCN6750, AHB/WPSS | QCA6390, PCIeRC0/MHI, MSI/SMMU/DMA 및 보드 펌웨어 경로 추가 |
| 터치 | Goodix/FocalTech, redwood_touch_post 이벤트 | 검증된 SW42902 GPIO-I2C와 디코더를 새 이벤트 형식으로 연결 |
| 버튼 | 기준 기기 PMIC/GPIO | 검증된 V60 PON/GPIO6 매핑을 새 버튼 이벤트로 연결 |
| 부트 | Mu-Silicium UEFI → EFI loader | V60용 UEFI 적합성 또는 기존 shim과 새 로더의 연결을 먼저 검증 |

근거 파일:

- kernel `xnu/files/bsd/dev/redwood_adreno.c`: GPU_CHIP_ID/GMU_CHIP 및
  `redwood_gpu_parameter()`의 GMEM·bank bit 반환값, GMU/SMMU 초기화.
- kernel `tools/mk-ath11k.sh`: CONFIG_ATH11K_AHB와 실제 빌드 파일 목록.
- kernel `wifi/lx/src/lx_bus.c`: WCN6750 하드웨어 선택과 AHB bus 할당.
  `pcic`/MSI 관련 코드가 있다는 사실만으로 QCA6390 PCIe/MHI 구현이 된 것은 아님.
- kernel `xnu/files/bsd/sys/redwood_input.h`: 정규화 좌표0..65535,
  `redwood_touch_post()` 및 버튼 이벤트 ABI.
- Rofik `kona-gpu.dtsi`, `drivers/gpu/msm/adreno-gpulist.h`의 A650 항목.

## 지원 상태 해석

저자는 기준 기기에서 GPU 합성과 Wi-Fi 연결을 보고합니다. 이번에 그 실기기 성능을
독립적으로 측정한 것은 아닙니다. 공개 README도 Bluetooth/셀룰러/충전과 앱용 Metal에
미완성 항목을 표시합니다. GPU로 UI를 그리는 것과 모든 Metal 앱이 동작하는 것은
구분해야 하며, userland에 Metal 구현 코드가 있다는 것만으로 완전 지원을 주장하지 않습니다.
기존 V60의 검은 배경/설정 정보 멈춤도 새 사용자 공간에서 재현·검증해야 합니다.

## 적용 순서

1. 새 소스에서 V60 부트/메모리/GOP/GIC 경로를 먼저 정하고 GPU·Wi-Fi를 끈 상태로
   커널·iOS16.5 사용자 공간의 일치하는 빌드를 만든다. V60 GUI 부팅을 첫 목표로 삼는다.
2. 기존에 검증한 SW42902 터치와 버튼의 알고리즘·하드웨어 매핑을 새 입력 ABI에 옮긴다.
   구형 커널의 kernel_hid_touch 주소나 iPhone 좌표 보정 상수를 그대로 쓰지 않는다.
3. A650 초기화와 펌웨어/SMMU를 단계별로 검증하고, GPU 작업 완료·readback을 확인한 뒤
   IOSurface/Mesa/EAGL 경로로 UI 합성을 켠다. 실제 프레임 시간으로 성능을 측정한다.
4. LinuxKPI/네트워크 연결부를 활용하되 QCA6390 PCIe/MHI 버스와 펌웨어 핸드셰이크를
   구현한 뒤 스캔·연결·실제 트래픽을 검증한다. 기존 amss20/board 파일의 ABI 적합성도 확인한다.
5. Bluetooth는 공개 소스에서도 별도 과제다. 기존 V60 UART 루프백 성공과 wireless3
   전원/HCI 작업을 새 커널의 인터페이스에 맞춰 이어 간다.

첫 커널 진단 boot.img를 생성했습니다. JSON의 `boot_gate.ready=false`는 여전히
V60 실기기/일치하는 iOS GUI/GPU/Wi-Fi 지원이 미검증이라는 뜻입니다. Redwood용 UEFI 주소·기본값·펌웨어를 V60에 넣지 않습니다.
현재 연결된 ADB 기기는 없었습니다. GPU 펌웨어 추가 수집/새 실기기 테스트는 하지 않았습니다.
