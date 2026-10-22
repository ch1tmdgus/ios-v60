#!/usr/bin/env python3
"""Apply the V60 overlay to a fresh pinned HackiOS-patched XNU tree.
Does not flash a device or edit upstream reference checkouts.
"""
from pathlib import Path
import argparse, shutil
HERE = Path(__file__).resolve().parent

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('xnu', type=Path)
    a = ap.parse_args()
    dest = a.xnu / 'bsd/dev'
    conf = a.xnu / 'bsd/conf/files'
    inp = dest / 'redwood_input.c'
    text = inp.read_text()
    anchor = '\tredwood_goodix_init();'
    if text.count(anchor) != 1 or 'v60_input_init' in text:
        raise SystemExit('refusing non-pristine upstream input source')
    native = HERE / 'native'
    for name in ['v60_touch_native.c', 'v60_keys.c', 'input_policy.h']:
        shutil.copyfile(native / name, dest / name)
    driver = (HERE.parent / 'v60-touch/v60_touch.c').read_text()
    old = 'int y=f->y[i]<12?12:(f->y[i]>2447?2447:f->y[i]);'
    assert driver.count(old) == 1
    driver = driver.replace(old, 'int y=f->y[i]; /* native framebuffer scales the whole physical panel */')
    (dest / 'v60_touch_impl.inc').write_text(driver)
    for name in ['protocol.h', 'gpio_i2c.h', 'clock_policy.h']:
        shutil.copyfile(HERE.parent / 'v60-touch' / name, dest / name)
    inp.write_text(text.replace(anchor, '\tv60_input_init();\n' + anchor).replace(
        '#define TOUCH_QUEUE', 'extern void v60_input_init(void);\n\n#define TOUCH_QUEUE', 1))
    with conf.open('a') as f:
        f.write('\nbsd/dev/v60_touch_native.c\tstandard\nbsd/dev/v60_keys.c\tstandard\n')
    print('V60 native input overlay applied:', a.xnu)

if __name__ == '__main__': main()
