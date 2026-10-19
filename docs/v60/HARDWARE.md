# LG V60 하드웨어 참조와 검증 범위

대상: LG V60 ThinQ 5G / timelm / SM8250. Android 커널 참조는
Rofik `7eab1a7189a7aba0a0c4a30ba616619e097ced52`와 사용자가 제공한 DT입니다.
다른 V60 변형/펌웨어에서의 동작을 보장하지 않습니다. 원시 DT/파티션은 포함하지 않습니다.

| 항목 | 주소/값 | 근거와 제한 |
| --- | --- | --- |
| 화면 | PA9c000000,1080×2460,stride4320 | test1 색상 출력, 이후 GUI 사용자 보고 |
| GICD |17a00000/10000 | DT 및 masked register read |
| GICR |17a60000/100000,stride20000 | DT 및 boot CPU register read |
| virtual/physical timer | INTID27/30 | DT. 별도 standalone IRQ 전달 검증은 미완료 |
| counter |19.2MHz | 실기기 register read |
| PSCI / SMCCC |1.1 / 1.1,SMC | 실기기 read. CPU_ON 광고와 SMP 실증은 다름 |
| UFS |01d84000,LUN0,4096-byte | test1 TUR/CAP/READ/GPT 성공 |
| userdata | sda28,첫 LBA005e2e28 | 해당 기기 관찰. 다른 기기엔 그대로 적용하지 않음 |
| restart reason |146bf65c | V60 기반 포트/DT |
| apps watchdog |17c10000,EN+08 | 기존 공개 V60 포트의 처리 유지 |
| SW42902 | I²C0x28,GENI13 a94000 | DT 활성 sw42902 / sw42000 disabled |
| 버스 핀 | SOUTH TLMM f500000,GPIO36 SDA/37 SCL | pinctrl 소스, touch4 실기기 동작 보고 |
| reset / IRQ | SOUTH GPIO38/39 | DT. IRQ active-low, polling |
| VCL / VDD | NORTH TLMM f900000,GPIO67/68 | DT/전원 코드. VCL→VDD→reset HIGH |
| SPMI | corec440000,obse600000,cnfgc40a000 | DT와 기존 observer 드라이버 대조 |
| 전원 / 볼륨다운 | PM8150 SID0 PON0810 bit0/1 | DT/qpnp-power-on. touch5 시험 대기 |
| 볼륨업 | PM8150 SID0 GPIO6,c510,PPID0c5 | named key_vol_up pinctrl state. touch5 시험 대기 |

제공 dump의 `gpio_keys` pinctrl-0=660은 PM8150 key_vol_up_default phandle660,
`pins=gpio6`에 연결됩니다. 별도 gpios 셀의 phandle66a는 controller661과 맞지 않아
그 부분의 dump 일관성은 미확인입니다. 버튼 매핑은 named state와 소스를 바탕으로
작성했고 GPIO input/enabled guard를 유지합니다. PMIC 설정을 강제로 쓰지 않습니다.

GPIO layout은 pin offset=id×1000,CFG mux bit2,OE bit9,IO IN bit0/OUT bit1입니다.
소프트웨어 I²C는 OUT을0에 고정해 LOW 또는 high-impedance만 만듭니다.
GSI firmware303/STATUS0/FIFO_IF_DISABLE1은 touch3에서 읽었습니다.

성공한 XNU 이미지의 메모리/ADT/CPU4개 구성은 보존했습니다. 이전 DT의 RAM 범위에
reserved 영역이 포함되므로 새 커널 관리 메모리나8코어 설정을 이 문서로 검증하지 않습니다.
배터리/USB/오디오/카메라/GPU의 V60 하드웨어 동작은 아직 확인하지 못했습니다.
