# Working on ios-v60

- Read `NOTES.md` before starting; it contains the project goal, hardware facts,
  current blocker, local artifact locations and the next concrete action.
- The user switches agents frequently. Update the active handoff in `NOTES.md`
  after each milestone and before stopping. Record commits, CI run IDs, exact
  build/boot results and unfinished work. Distinguish observations from guesses.
- Commit completed changes. Use `gh` for GitHub Actions kernel builds and logs.
  Local single-file cross-compiles are useful before spending a CI run.
- Device bring-up uses temporary `fastboot boot` images. Preserve Android,
  bootloaders, recovery, modem, persist and all backups. Prior upstream-device
  destructive approvals do not apply to the current V60. Require explicit user
  authorization for each destructive device operation. Do not flash just to test.
- User confirmed touch4 touch and touch5 buttons. Distinguish those reports
  from unmeasured performance and unimplemented Wi-Fi/Bluetooth.
  Never describe target60Hz as measured60fps or GUI icons as working hardware.
- The fixed-image builder requires the original baseline SHA; full matching
  XNU build provenance is still unresolved. Preserve the user base port changes.
- Keep device dumps, serial numbers and downloaded Apple artifacts out of git.
