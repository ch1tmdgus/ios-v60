#!/usr/bin/env python3
"""Prepare a pinned private userland checkout for the macOS CPU-rendered build.

Run fetch_ios165.py first. No Apple files or resulting images belong in Git.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import build


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--workspace', type=Path, required=True)
    ap.add_argument('--kernel-repo', type=Path, required=True)
    ap.add_argument('--sdk', type=Path, help='pinned iPhoneOS16.5 SDK; rebuild the CPU GUI adapters on macOS')
    a = ap.parse_args()
    for repo, commit in ((a.workspace, build.USERLAND_COMMIT), (a.kernel_repo, build.KERNEL_COMMIT)):
        assert subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip() == commit
    script = a.workspace / 'userland/build.sh'
    original = subprocess.check_output(['git', '-C', str(a.workspace), 'show', 'HEAD:userland/build.sh'], text=True)
    old = '\tcp obj/hello_dyn obj/svc_test obj/surf_test obj/drawable_test obj/rwtouch obj/mg_test root/bin/'
    new = ('\tfor prog in hello_dyn svc_test surf_test drawable_test rwtouch mg_test; do\n'
           '\t\tif [ -f obj/$prog ]; then cp obj/$prog root/bin/; else echo "optional prebuilt test absent: $prog"; fi\n'
           '\tdone')
    zone = '\tzone=$(timedatectl show -p Timezone --value 2>/dev/null); zone=${zone:-UTC}'
    assert original.count(old) == 1 and original.count(zone) == 1
    adapted = original.replace(old, new).replace(zone, '\tzone=Asia/Seoul')
    if script.read_text() not in (original, adapted):
        ap.error('unexpected local build.sh changes; preserving them')
    script.write_text(adapted)
    # Upstream reuses the outer job filename variable `name` in this loop.
    # With REST=1 every on-demand plist is consequently overwritten into one
    # file named RWA_PACKED16, which launchd cannot import as a plist.
    jobs = a.workspace / 'userland/mkjobs.py'
    jobs_original = subprocess.check_output(['git', '-C', str(a.workspace), 'show', 'HEAD:userland/mkjobs.py'], text=True)
    jobs_adapted = jobs_original.replace("for name in ('RWA_PACKED16',):", "for env_name in ('RWA_PACKED16',):").replace(
        "if os.environ.get(name):", "if os.environ.get(env_name):").replace(
        "job.setdefault('EnvironmentVariables', {})[name] = os.environ[name]",
        "job.setdefault('EnvironmentVariables', {})[env_name] = os.environ[env_name]")
    assert jobs_original.count("for name in ('RWA_PACKED16',):") == 2
    if jobs.read_text() not in (jobs_original, jobs_adapted):
        ap.error('unexpected local mkjobs.py changes; preserving them')
    jobs.write_text(jobs_adapted)
    shutil.copyfile(a.kernel_repo / 'xnu-env.sh', a.workspace / 'xnu-env.sh')
    env = dict(os.environ, PREBUILT='1', REST='1', SHARE='1', GL='')
    env['PATH'] = '/opt/homebrew/opt/gnu-sed/libexec/gnubin:/opt/homebrew/opt/coreutils/libexec/gnubin:' + env['PATH']
    subprocess.run(['bash', 'userland/build.sh'], cwd=a.workspace, env=env, check=True)
    if a.sdk:
        sdk_repo = a.sdk.resolve().parent
        assert subprocess.check_output(['git', '-C', str(sdk_repo), 'rev-parse', 'HEAD'], text=True).strip() == '0222fd5413cf4b9af096f37b4621afa2688572f7'
        dest = a.workspace / 'userland/root/lib'
        for source, library in [('inject.c', 'libredwood.dylib'), ('inject_sb.c', 'libredwood-sb.dylib')]:
            subprocess.run(['xcrun', 'clang', '-target', 'arm64-apple-ios16.5', '-isysroot', str(a.sdk),
                            '-O1', '-dynamiclib', '-install_name', '/redwood/lib/' + library,
                            '-framework', 'CoreFoundation', '-framework', 'IOKit', '-lobjc',
                            '-o', str(dest / library), str(a.workspace / 'userland/src' / source)], check=True)
        subprocess.run([sys.executable, str(a.workspace / 'tools/apfs/mkapfs.py'), '-z', '--plain', 'fw',
                        '-o', str(a.workspace / 'userland/root.apfs'), 'redwood=' + str(a.workspace / 'userland/root')], check=True)


if __name__ == '__main__':
    main()
