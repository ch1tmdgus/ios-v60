# V60 (LG timelm, SM8250) 포팅 메모

`apply-v60-port.py` 가 적용하는 변경과 첫 부팅 절차.

## 첫 부팅 목표
XNU 가 부팅해서 프레임버퍼에 콘솔 로그를 찍는 것. 터치/버튼/USB/배터리 드라이버는 sweet(SM7150) 전용이라 끈다.

## payload 만들기
    python3 tools/mkadt.py shim/out/v60.adt --highmem
    python3 tools/mkpayload.py xnu/out/kernel.development.sweet shim/out/v60.adt shim/out/payload.bin \
        --cmdline 'debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 launchd_unsecure_cache=1 wdt=-1 sweettouch=0 sweetkeys=0 sweetusb=0'
    DTB=/path/to/v60.dtb shim/build.sh ../shim/out/payload.bin     # -> shim/out/shim-boot.img
    fastboot boot shim/out/shim-boot.img

`cpus=1` 이 첫 부팅의 안전한 값이다. 안정되면 `cpus=4`(A55 4개) 로 올린다.

## V60 에서 sweet 와 달라서 바꾼 것
| 항목 | sweet | V60 |
| --- | --- | --- |
| 패널 | 1080x2400 (video mode) | 1080x2460 (SW43103 DSC command mode, 프레임버퍼 쓰기만으로 갱신됨 - 실기기 확인) |
| 프레임버퍼 | 0x9c000000 | 0x9c000000, 크기 0x1200000 |
| 가상 타이머 | INTID 19 | INTID 27 (PPI 11) |
| IMEM restart_reason | 0x146aa65c | 0x146bf65c |
| CPU | A55 x6 + A76 x2 | A55 x4 (MPIDR 0x000-0x300) + A77 x4 (0x400-0x700) |
| 워치독 | - | ABL 이 켜 둔 apps wdt(0x17c10000) 를 shim 에서 끔 (EN=0, 실기기 확인) |

같은 값: GICD 0x17a00000, GICR 0x17a60000, UFS 호스트 0x1d84000, PSHOLD 0xc264000, userdata 는 LUN 0 의 GPT 이름 "userdata".

## 아직 V60 용으로 안 한 것
- 터치: V60 은 LG SiW SW42902 (I2C 0x28, GENI I2C 0xa94000). Goodix GT9896 드라이버(0060, 0063)는 쓸 수 없음.
- 버튼/PMIC: PM8150 계열 (sweet 은 PM6150). 볼륨업은 PMIC GPIO 6.
- 배터리/USB: sweet 전용. `sweetbattery`(기본 꺼짐), `sweetusb=0`.
- 비-highmem 메모리(`PHYS_BASE=0xb0000000`)는 V60 의 ramoops 영역과 겹치므로 `--highmem` 만 사용.
