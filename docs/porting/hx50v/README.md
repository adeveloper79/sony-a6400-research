# DSC-HX50V work (experimental)

> ⚠️ **Experimental / different BIONZ generation.** The DSC-HX50V is an ARMv6
> ("CE Linux 2.6.29.6-rt24 SMP", Sony BIONZ, CXD4132-class) camera — not the
> ARMv7 BIONZ X / CXD90045 family this repository targets. Nothing here is
> merged into `main`, nothing here is verified to work, and none of it applies
> to the a6400/a6600 target bodies.

Findings from an offline analysis of a user-owned DSC-HX50V dump (firmware
2.31.51, read-only service-shell dumps). Goal was: understand the body family,
see whether the a6400 emulator approach transfers, and attempt a port — it does
not transfer cleanly.

## Contents

| File | What it is |
|---|---|
| `docs/porting/hx50v/hx50v-firmware-dump.md` | Dump analysis: updater + main kernels, `Loader2.bin` bootloader (EXBL/"Astra" V85.00.03), rootfs inventory (35 languages, 35 `string_*.uxc`), live `/proc` transcript, RAW analysis summary |
| `scripts/porting/hx50v_boot.py` | Experimental boot of the updater kernel in the a6400's Unicorn machine (reaches MMU-on VA mode into `__mmap_switched`; stops in the low-memory/vector handoff) |
| `scripts/porting/hx50v_boot_zimage.py` | Full-zImage boot variant (self-decompressor runs; reaches the same wall with a cleaner path) |
| `scripts/porting/extract_ext4.py` | Extract Sony ext2/3/4 partition images with the 1024-byte boot record |
| `scripts/porting/read_fat.py` | List Sony FAT12/16 partition contents |

## Status

- Emulator reaches MMU-on VA-mode execution through `__mmap_switched` and stops in
  the low-memory/vector handoff — **the same class of blocker the a6400 needed
  months of dedicated work on.** Not a flag; a bespoke ARMv6 TLB/page-walk port.
- No boot-to-shell milestone. Nobody should expect one from this code.
- The camera itself was never modified; all dumps were read-only.

## What is NOT in this branch

- Firmware images, flash dumps, bootloaders, updater executables (proprietary)
- Sony service manuals (copyrighted; cited, not redistributed)
- Kernel `kallsyms` dumps (derived artifacts)
