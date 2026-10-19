#!/usr/bin/env python3
"""ios-sweet 포크(체크아웃) 에 LG V60(timelm, SM8250) 용 변경을 적용한다.

사용법 (포크한 저장소 루트에서):
    python3 apply-v60-port.py [저장소경로]

- 이미 적용된 항목은 건너뛰므로 여러 번 실행해도 안전하다.
- 기대한 원본 문자열이 없으면 (저장소가 달라졌거나 일부만 적용된 경우) 오류로 멈춘다.
- xnu/patches/*.patch 안의 숫자만 바꾸므로 hunk 줄 수는 그대로다. 일부 패치에 UTF-8 이 아닌
  바이트가 있어서 latin-1 로 읽고 쓴다.

변경 근거는 V60 실기기에서 덤프한 DT/iomem/cmdline 과 shim 테스트(색 전환, 워치독) 결과다.
"""
import os
import sys

root = sys.argv[1] if len(sys.argv) > 1 else '.'

EDITS = {
    # ---- 부팅 shim -------------------------------------------------------
    'shim/console.h': [
        ('#define FB_H\t\t2400', '#define FB_H\t\t2460'),            # SW43103 1080x2460
    ],
    'shim/shim.c': [
        ('0x146aa65cUL\t/* qcom,msm-imem restart_reason@65c */',
         '0x146bf65cUL\t/* V60 live DT: qcom,msm-imem@146bf000 + restart_reason@65c */'),
        ('#define VTIMER_INTID\t\t19\t', '#define VTIMER_INTID\t\t27\t'),   # <GIC_PPI 11>
        ('#define PTIMER_INTID\t\t18\t', '#define PTIMER_INTID\t\t30\t'),   # <GIC_PPI 14>
        ('#define PSHOLD\t\t\t0x0c264000UL',
         '#define PSHOLD\t\t\t0x0c264000UL\n'
         '#define WDT_BASE\t\t0x17c10000UL\t/* V60 qcom,wdt: ABL leaves the apps watchdog armed (~1 min reset); EN is +0x08 */'),
        ('\tstruct mmu_info mi;\n\n\t/* An xnu payload in the ramdisk means: boot it (MMU stays off). */',
         '\tstruct mmu_info mi;\n\n'
         '\t/* V60: disable the apps watchdog first, as Linux does on suspend. Verified on device (survives 180 s). */\n'
         '\twr32(WDT_BASE + 0x08, 0);\n'
         '\t__asm__ volatile("dsb sy" ::: "memory");\n\n'
         '\t/* An xnu payload in the ramdisk means: boot it (MMU stays off). */'),
    ],
    # 스톡 boot.img 대신 DTB 경로를 환경변수로 지정할 수 있게 한다: DTB=/path/to/dtb shim/build.sh ...
    'shim/build.sh': [
        ('--dtb ../dtb/boot_unpacked/dtb', '--dtb "${DTB:-../dtb/boot_unpacked/dtb}"'),
    ],
    # ---- Apple Device Tree 생성기 -------------------------------------------
    'tools/mkadt.py': [
        ('FB, FB_SIZE = 0x9c000000, 0x1700000',
         'FB, FB_SIZE = 0x9c000000, 0x1200000   # V60: reserved-memory cont_splash_region (18 MiB)'),
        ('FB_W, FB_H, FB_STRIDE = 1080, 2400, 1080 * 4',
         'FB_W, FB_H, FB_STRIDE = 1080, 2460, 1080 * 4   # V60 SW43103 panel'),
        ('VTIMER_INTID = 19\n',
         'VTIMER_INTID = 27   # V60 DT: arch timer virt = <GIC_PPI 11> -> INTID 27 (sweet used 19)\n'),
        ('u32(2304000000 if core >= 6 else 1804800000)',
         'u32(2419200000 if core >= 4 else 1804800000)'),
        ('(0-5 Cortex-A55,\n    # 6-7 Cortex-A76)',
         '(0-3 Cortex-A55,\n    # 4-7 Cortex-A77 on the V60 SM8250)'),
    ],
    # ---- XNU 패치 (숫자만 변경) ---------------------------------------------
    'xnu/patches/0004-sweet-machine-config.patch': [
        ('#define SWEET_VTIMER_INTID        19', '#define SWEET_VTIMER_INTID        27'),
    ],
    'xnu/patches/0010-sweet-early-framebuffer-debug.patch': [
        ('MOV64\tx0, (0x9c000000 + 2368 * 4320)', 'MOV64\tx0, (0x9c000000 + 2428 * 4320)'),  # 2460-32 행
        ('#define EARLY_HEIGHT 2400u', '#define EARLY_HEIGHT 2460u'),
    ],
    'xnu/patches/0058-sweet-hid-event-services.patch': [
        ('(((int64_t)y << 16) / 2400)', '(((int64_t)y << 16) / 2460)'),
    ],
    # iPhone X(1125x2436) 화면을 V60 패널(1080x2460) 가운데에 표시: 좌우 22px 잘림, 위아래 12px 검은 띠.
    'xnu/patches/0061-sweet-iphone-x-display-size.patch': [
        ('(((int64_t)y << 16) / 2400)', '(((int64_t)y << 16) / 2460)'),
        ('fLogicalH = native ? 2400 : 2436;', 'fLogicalH = native ? 2460 : 2436;'),
        ('fOffY = (fLogicalH - 2400) / 2;',
         'fOffY = (fLogicalH - 2460) / 2;   /* -12: 2436-row iPhone X view letterboxed in 2460 rows */'),
        ('if (!native && fWidth <= SWEET_FB_LOGICAL_W && fHeight <= SWEET_FB_LOGICAL_H) {',
         'if (!native && fWidth <= SWEET_FB_LOGICAL_W) {'),
    ],
}

NOTES = '''# V60 (LG timelm, SM8250) 포팅 메모

`apply-v60-port.py` 가 적용하는 변경과 첫 부팅 절차.

## 첫 부팅 목표
XNU 가 부팅해서 프레임버퍼에 콘솔 로그를 찍는 것. 터치/버튼/USB/배터리 드라이버는 sweet(SM7150) 전용이라 끈다.

## payload 만들기
    python3 tools/mkadt.py shim/out/v60.adt --highmem
    python3 tools/mkpayload.py xnu/out/kernel.development.sweet shim/out/v60.adt shim/out/payload.bin \\
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
'''


def main():
    changed = 0
    for rel, pairs in EDITS.items():
        path = os.path.join(root, rel)
        if not os.path.isfile(path):
            sys.exit(f'[오류] 파일이 없습니다: {path} (ios-sweet 저장소 루트에서 실행하세요)')
        with open(path, encoding='latin-1', newline='') as f:
            s = f.read()
        for old, new in pairs:
            if s.count(new) >= 1:
                continue  # 이미 적용됨 (new 가 old 를 포함하는 항목도 여기서 걸러진다)
            elif s.count(old) == 1:
                s = s.replace(old, new)
                changed += 1
            else:
                sys.exit(f'[오류] {rel}: 기대한 원본 문자열을 찾지 못했습니다:\n  {old!r}')
        with open(path, 'w', encoding='latin-1', newline='') as f:
            f.write(s)
    with open(os.path.join(root, 'V60-PORT.md'), 'w', encoding='utf-8') as f:
        f.write(NOTES)
    print(f'완료: {changed}곳 변경 (이미 적용된 곳은 건너뜀). 메모: V60-PORT.md')


if __name__ == '__main__':
    main()
