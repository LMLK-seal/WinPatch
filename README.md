# WinPatch

> **A small DOS command-line tool that automatically fixes the classic Windows 98 **"Windows Protection Error"** boot crash caused by having more RAM installed than the stock 9x kernel handles cleanly It edits `SYSTEM.INI` for you — no manual text editing, no guessing at hex values.**

![WinPatch Screenshot](https://github.com/LMLK-seal/WinPatch/blob/main/Logo.png?raw=true)


## The problem

Windows 98's memory manager (`VMM32.VXD`) has a well-documented bug: on machines with roughly 512MB of RAM or more, it can miscount physical memory during the boot handoff from real mode to protected mode. The result is a hard crash — a "Windows Protection Error" — right as `WIN.COM` hands control to `VMM32.VXD`, before the desktop ever loads.

The documented fix (Microsoft KB253912 / KB184447 / KB304943) is to explicitly cap how much RAM Windows' memory manager is allowed to see, via two settings in `SYSTEM.INI`:

```ini
[386Enh]
MaxPhysPage=40000

[vcache]
MinFileCache=131072
MaxFileCache=262144
```

Calculating the right hex value and safely editing an existing `SYSTEM.INI` without breaking other entries is fiddly to do by hand, especially from a bare DOS prompt. WinPatch does it in one command.

## What it does

1. Reads `C:\WINDOWS\SYSTEM.INI` into memory.
2. Backs up the untouched original to `C:\WINDOWS\SYSTEM.BAK`.
3. Sets `MaxPhysPage` under `[386Enh]` — adding the section if it's missing, updating the value in place if it already exists.
4. Sets `MinFileCache` / `MaxFileCache` under `[vcache]`, automatically scaled to stay under 80% of the declared RAM ceiling (going over that ratio trades the Protection Error for a different crash — "Insufficient memory to initialize Windows").
5. Writes the result back to `SYSTEM.INI`, leaving every other line in the file untouched.

Safe to run more than once: re-running with a different value updates the existing settings instead of duplicating lines.

## Important: this sets a target ceiling, not necessarily your physical RAM

- **Up to ~1024MB installed:** pass your actual RAM and you're done.
- **More than ~1024MB installed (e.g. 2GB):** stock `VMM32.VXD` is documented as unreliable regardless of what value you declare. You need to pass a **reduced** ceiling below your real RAM — commonly somewhere in the 768–1000 range — effectively hiding the excess from Windows rather than accurately reporting it.

WinPatch will warn you if you pass a value above 1024, but it won't silently override your input — it does exactly what you tell it.

## Requirements

- A Windows 98 installation that currently fails to boot with a Windows Protection Error (i.e. Setup has already run and `C:\WINDOWS\SYSTEM.INI` exists).
- A way to reach a DOS prompt on the affected machine — either:
  - Restart, press **F8**, and choose **"Command Prompt Only"** (Safe Mode command line), or
  - Boot from a DOS USB stick / floppy and navigate to `C:\`.

## Building

WinPatch is a real-mode 16-bit DOS executable, built from a single C file with no external dependencies.

**Option 1:**

Download the complete exe file from the Releases.

**Option 2:**

Download the .c file and build it as follows:

**Open Watcom** (recommended — free, runs on modern Windows or Linux, cross-compiles straight to DOS):

```
wcl -bt=dos -ml winpatch.c
```

**Turbo C++ / Borland C++** (under DOSBox or a real DOS machine):

```
tcc winpatch.c
```

Either produces `WINPATCH.EXE`.

## Usage

Copy `WINPATCH.EXE` onto your boot USB stick, boot the affected machine to a DOS prompt, and run:

```
WINPATCH <target_RAM_ceiling_in_MB>
```

Example, for a machine with 1GB installed:

```
WINPATCH 1024
```

Example output:

```
Backed up original to C:\WINDOWS\SYSTEM.BAK
Added MaxPhysPage=40000 to [386Enh]
Added new [vcache] section with MinFileCache=131072
Added MaxFileCache=262144 to [vcache]

Done. MaxPhysPage=40000 (1024 MB ceiling), MinFileCache=131072, MaxFileCache=262144
Reboot to apply the changes.
```

Reboot normally afterward (not back into DOS) and let it continue into Windows.

## Restoring the original

If anything looks wrong after patching, boot back to DOS and restore the backup:

```
COPY /Y C:\WINDOWS\SYSTEM.BAK C:\WINDOWS\SYSTEM.INI
```

## Limitations

- Targets `C:\WINDOWS\SYSTEM.INI` specifically; it doesn't search for a Windows install on another drive letter.
- Assumes the file fits comfortably in DOS conventional memory (real-world `SYSTEM.INI` files — a few hundred lines — are well within the 4000-line limit this tool supports).
- Doesn't attempt to detect installed RAM automatically (no `INT 15h` BIOS calls) — you provide the target value. This is intentional: auto-detection code is a much easier place to introduce a bug that's hard to verify across the range of period BIOSes this tool is likely to run against.

## License
-  This project is released under the MIT License.
