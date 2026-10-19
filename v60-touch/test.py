#!/usr/bin/env python3
"""Host packet tests + QEMU Apple-varargs bridge test. No device access."""
import argparse,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
r=Path(__file__).resolve().parent;pre=os.environ.get('CROSS_COMPILE','aarch64-elf-')
def run(*cmd):subprocess.run(list(map(str,cmd)),check=True,timeout=30)
run('clang','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',r/'protocol_test.c','-o',a.out/'protocol_test')
run(a.out/'protocol_test')
run('clang','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',r/'gpio_i2c_test.c','-o',a.out/'gpio_i2c_test')
run(a.out/'gpio_i2c_test')
run('clang','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',r/'wireless_probe_test.c','-o',a.out/'wireless_probe_test')
run(a.out/'wireless_probe_test')
run(pre+'gcc','-c',r/'printf_bridge.S','-o',a.out/'bridge.o')
run(pre+'gcc','-c',r/'tests/printf_bridge_test.S','-o',a.out/'bridge_test.o')
run(pre+'ld','-T',r/'tests/printf_bridge_test.ld',a.out/'bridge_test.o',a.out/'bridge.o','-o',a.out/'bridge_test.elf')
run('qemu-system-aarch64','-M','virt','-cpu','cortex-a76','-m','256','-display','none','-serial','none','-monitor','none','-semihosting-config','enable=on,target=native','-kernel',a.out/'bridge_test.elf')
print('Apple arm64 varargs stack slots, frame pointer and return address: PASS')
