---
name: Mac-mini-M6-fan-control-LLM
version: 1.1.0
license: MIT
author: Argus (Claude Opus 5.5) for Hans-Dieter Zucht
tested_on: Mac mini M6 (Mac18,5), macOS 27.0.1
---

# Mac mini M6 fan control for local LLMs

**A tiny fan controller for Apple Silicon Macs that listens to the sensors that actually heat up when you run a local LLM: the voltage regulators next to the SoC.**

## Why this exists

> I run local LLMs on my new Mac mini M6. During long runs I noticed that the fan barely spins up with Apple's default settings, while the sensors in the power delivery, which by their SMC names belong to the voltage regulators next to the chip, run hot. On my M6, Macs Fan Control showed the fan but could not control it (October 2026). That left an unmet need: protect a newly purchased machine from heat-related wear under sustained load, because Apple's default fan profile is tuned for silence and seems a little too relaxed for this kind of work.
>
> — Hans-Dieter Zucht

Running a dense 27B model in LM Studio on a Mac mini M6, the voltage-regulator sensor (SMC key `TVD0`, by its usual naming) reached **94.9 °C** while Apple's automatic fan control kept the fan at **1,031 rpm**, a hair above its 1,000 rpm minimum. In this run Apple's controller did not respond to these sensors. This tool takes over when the hottest of them passes 60 °C and hands control back below 55 °C.

First run under the same load:

```
17:52:41 TVD0 89.5 °C -> 4900 rpm
17:52:51 TVD0 82.5 °C -> 4505 rpm
17:53:05 TVD0 77.9 °C -> 3790 rpm
17:53:16 TVD0 76.6 °C -> 3588 rpm
```

13 K cooler within 35 seconds, then settled.

**Also useful for gaming.** During 25 minutes of Civilization VII (Steam) the voltage regulators were again the hottest group in every reading, 6–7 K above the GPU. Gaming is a steady load: fanguard settled at about 76 °C and 3,500 rpm and stayed there, with one short peak at 82 °C answered by 4,500 rpm.

How fast local models run on this machine, and which ones produced the heat: [Mac-Mini-M6-LLM-benchmarks](https://github.com/HDZucht/Mac-Mini-M6-LLM-benchmarks).

## What you get

| Part | What it does |
|---|---|
| `fanguard` (C, ~240 lines) | reads SMC temperatures and fan state; `run` is the controller service |
| `install.sh` | builds and installs `fanguard run` as a LaunchDaemon (root) |
| `menubar/` (Swift, ~75 lines) | menu bar item `76° · 3588` with a mode menu, registers itself as a login item |
| `sensorprobe.c` | lists every HID and SMC temperature sensor of your Mac |

No dependencies beyond the Xcode Command Line Tools (`clang`, `swiftc`). No Homebrew, no kernel extension.

## How it decides

```
 rpm
 max ┤                         ┌──────────
     │                       ╱
     │                     ╱      linear from minimum to maximum
     │                   ╱
 min ┤ Apple automatic ╱
     └──────────────┬─────────┬──────────── hottest deciding sensor (°C)
                   60        85
                (back to Apple below 55)
```

- **Deciding sensors** are set by key prefix in `fanguard.conf`: by default `TVD` (voltage regulators), `Tg` (GPU), `Tp` (performance cores). The hottest one drives the fan.
- **Never slower than Apple.** On taking over, fanguard records the speed Apple's controller was running and never goes below it.
- **Always hands back.** Below `start − hysteresis`, on `SIGTERM`, `SIGINT` and on uninstall the fans return to automatic (`F*md = 0`).
- **Modes** (file `/Users/Shared/mac-fan-guard/mode`): `curve` (default, as above); a number such as `50` or `100` sets that percentage as a **minimum**, and the curve can still raise it when things get hot; `auto` leaves everything to Apple, including under load.
- **Peak hold:** the controller works with the highest reading of the last 10 seconds (`hold` in `fanguard.conf`). A rise acts at once, a fall only after the hold time, which smooths the short load bursts of LLM inference.
- **Rises readily, falls reluctantly:** a new target is written when the curve has moved more than 50 rpm up or 150 rpm down, so the fan does not hunt.
- **All fans.** It reads `FNum`; on Macs with two fans each gets the same fraction of its own range.

## ⚠️ Use at your own risk

This tool writes to the System Management Controller (SMC) of your Mac through an undocumented interface. It has been tested on a single Mac mini M6. It is provided **as is, without any warranty**, under the MIT License. You alone are responsible for running it on your hardware; the authors accept no liability for damage, data loss, voided warranties or any other consequence. If you are unsure, use only the read-only commands (`fanguard status`, `fanguard sensors`, `sensorprobe`).

## Install

```bash
git clone https://github.com/HDZucht/Mac-mini-M6-fan-control-LLM.git
cd Mac-mini-M6-fan-control-LLM

# 1. Look first (no root): which sensors does your chip have, which ones run hot?
clang -O2 -o fanguard fanguard.c -framework IOKit -framework CoreFoundation
./fanguard status
./fanguard sensors

# 2. Does your SMC accept fan writes? 12 s at maximum, then back to automatic.
#    Quit TG Pro, Macs Fan Control or any other fan tool first.
sudo ./fanguard test

# 3. Install the service.
sudo ./install.sh

# 4. Optional: menu bar item.
menubar/build.sh
```

## Use

| You want | Do |
|---|---|
| see what is going on | `fanguard status`, or look at the menu bar |
| full speed before a long LLM run | `echo 100 > /Users/Shared/mac-fan-guard/mode` or menu → *Full speed (100 %)* |
| a quieter but still guarded floor | `echo 50 > /Users/Shared/mac-fan-guard/mode` or menu → *At least 50 %* |
| back to the curve | `echo curve > /Users/Shared/mac-fan-guard/mode` |
| leave everything to Apple (no protection) | `echo auto > /Users/Shared/mac-fan-guard/mode` |
| follow the controller | `tail -f /var/log/mac-fan-guard.log` |
| change thresholds or sensors | edit `/usr/local/mac-fan-guard/fanguard.conf`, then `sudo launchctl kickstart -k system/io.github.hdzucht.mac-fan-guard` |
| uninstall | `sudo ./install.sh --remove` and `menubar/build.sh --remove` |

The menu bar shows the hottest deciding sensor and the fan speed, orange from 70 °C, red from 85 °C. A suffix shows the service mode: ` A` for `auto`, ` 50%` or ` 100%` for a minimum, none for `curve`.

## Sensors on the Mac mini M6

Measured on a Mac18,5 with macOS 27.0.1: one fan (1,000 to 4,900 rpm), 148 SMC temperature keys, 43 HID temperature sensors. Apple does not document the keys; the meanings below follow the naming conventions other SMC tools use and are an interpretation, not a specification.

| Prefix | Probably | Idle | LLM load, Apple automatic |
|---|---|---|---|
| `TVD*`, `TCMb` | voltage regulators next to the SoC | 57.7 °C | **94.9 °C** at 1,031 rpm (`TVD0`) |
| `Tg*` | GPU | 45.5 °C | |
| `Tp*`, `TPD*` | performance cores | 45–50 °C | |
| `Te*` | efficiency cores | 45–49 °C | |
| `TN*` | SSD | 41–42 °C | |

Fan writes on the M6 work like on M1 to M4: `F0md = 1` (manual), then a float target in `F0Tg`. If the first write is refused, fanguard sets `Ftst = 1` and retries, a fallback other fan tools use on some Apple Silicon models; the M6 did not need it.

## Why bother

Apple does not publish limits for these parts. Power semiconductors of this kind are commonly rated for junction temperatures of 125 °C or more, so 95 °C is most likely within spec. Two general rules still argue for cooling: component ageing accelerates with temperature (a common rule of thumb is a factor of about two per 10 K), and every LLM run that swings the board from about 45 to 95 °C and back is a thermal cycle for the solder joints. A fan at 4,900 rpm costs some noise.

## Limits

- Tested on one machine: Mac mini M6 (Mac18,5). The code handles several fans and falls back to all `T*` sensors when the configured prefixes do not exist, but MacBook Pro and M1–M5 Macs are untested. Run `./fanguard sensors` and `sudo ./fanguard test` first.
- The SMC interface is undocumented and can change with any macOS update.
- One fan controller at a time. TG Pro, Macs Fan Control (with control enabled) or similar tools will fight over the fan.
- The mode file in `/Users/Shared` is world-writable by design so the menu bar works without root. Anyone logged in can switch the mode, including to `auto`, which turns the protection off; no mode drives the fan below its minimum speed.

Provided as is, without warranty (MIT License).

## Working on it with Claude Code

`CLAUDE.md` tells Claude Code how to install, adapt and change this tool safely. Open the folder in Claude Code and ask, for example, *"install this on my Mac"* or *"adapt the sensors for my MacBook Pro"*.

## Credits

Written by **Argus**, a Claude Opus 5.5 instance working in Hans-Dieter Zucht's research vault, on 4 October 2026, after a local-LLM benchmark showed the voltage regulators running hot. Hans-Dieter Zucht ran the hardware tests and decided to publish it.
