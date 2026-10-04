# CLAUDE.md — instructions for Claude Code

This repository is a small fan controller for Apple Silicon Macs (`fanguard.c`), a LaunchDaemon installer (`install.sh`), a menu bar item (`menubar/`) and a read-only sensor lister (`sensorprobe.c`). Read `README.md` first.

The user's hardware is at stake. Follow the order below and show the user each result before the next step.

## Installing for a user

1. **Check the machine.** `uname -m` must be `arm64`. Note `sysctl -n hw.model` and `sw_vers -productVersion`. Confirm the Xcode Command Line Tools exist (`clang --version`, `swiftc --version`); if not, ask the user to run `xcode-select --install`.
2. **Look for competing fan tools.** `ls /Applications | grep -i -E "fan|tg pro"`. If TG Pro, Macs Fan Control or similar is running with fan control enabled, ask the user to quit it. Two controllers fight over the fan.
3. **Read before writing (no root).**
   ```bash
   clang -O2 -o fanguard fanguard.c -framework IOKit -framework CoreFoundation
   ./fanguard status
   ./fanguard sensors
   ```
   Report the fan count, min/max rpm and which sensors are hottest. If the default prefixes `TVD,Tg,Tp` mark no sensors, fanguard falls back to all `T*` keys; suggest prefixes from the `sensors` output instead (the group that is hottest under load).
4. **Write test (root, the user runs it).** `sudo ./fanguard test` sets maximum for 12 s and returns to automatic. Ask the user whether the fan audibly spun up and paste the output. Proceed only if the rpm rose towards the maximum. If the write fails, stop and report; do not experiment with other SMC keys.
5. **Install.** The user runs `sudo ./install.sh`. Then check `tail /var/log/mac-fan-guard.log` and `./fanguard status`.
6. **Menu bar (optional, no root).** `menubar/build.sh`. Mention that the app registers itself as a login item.
7. A behavioural security tool (e.g. BlockBlock) will report the new LaunchDaemon `io.github.hdzucht.mac-fan-guard`. That is expected; tell the user before they see it.

Never run `sudo` yourself without the user's explicit go-ahead, and never install while another fan tool is controlling the fan.

## Adapting

| Request | Where |
|---|---|
| other thresholds | `fanguard.conf` (`start`, `full`, `hysteresis`, `interval`); on an installed system edit `/usr/local/mac-fan-guard/fanguard.conf` and restart with `sudo launchctl kickstart -k system/io.github.hdzucht.mac-fan-guard` |
| other deciding sensors | `sensors=` in the same file, comma-separated key prefixes; check them with `./fanguard sensors` |
| other Mac (two fans) | already handled through `FNum`; verify with `status` and `test` |
| menu bar shows a different sensor | `fanguard line KEY` returns that key; change the arguments in `menubar/FanGuardMenu.swift` (`readLine`) |
| a trigger such as "LM Studio has a model loaded → 100 %" | write a small script that writes `100` or `curve` to `/Users/Shared/mac-fan-guard/mode`; do not add process watching to the root daemon |

## Rules for changing the code

- **The controller may only raise the fan above Apple's automatic speed.** Keep the floor (`floor_rpm`) and never write a target below the fan's own minimum (`F*Mn`).
- **Every exit path returns to automatic.** Keep `set_auto()` in the signal path, at the end of `run()` and in `install.sh --remove`. Any new exit path needs it too.
- **Root code stays small.** The daemon reads the SMC and one text file. Anything with a user interface or network belongs in an unprivileged process that writes the mode file.
- **Undocumented SMC keys:** read freely, write only `F*md`, `F*Tg` and `Ftst`. Do not write other keys.
- After a change: build with `-Wall` without warnings, run `status`, `sensors`, `line`, then ask the user for `sudo ./fanguard test` and a reinstall.

## Layout

```
fanguard.c        SMC access, sensor enumeration, controller (commands: status, sensors, line, set, auto, test, run)
fanguard.conf     thresholds and deciding sensor prefixes (installed copy is kept on update)
install.sh        sudo ./install.sh [--remove]   → /usr/local/mac-fan-guard, /Library/LaunchDaemons/io.github.hdzucht.mac-fan-guard.plist
menubar/          FanGuardMenu.swift + build.sh   → ~/Applications/Fan Guard.app (login item)
sensorprobe.c     read-only listing of all HID and SMC temperature sensors
/Users/Shared/mac-fan-guard/mode   curve | auto | 0-100, read by the daemon every interval
/var/log/mac-fan-guard.log         controller log
```
