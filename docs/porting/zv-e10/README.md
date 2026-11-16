# ZV-E10 porting work (Sony ZV-E10 / CXD90045, BIONZ X)

**Status:** static analysis complete; **no camera has been touched; no firmware modified.**

This branch collects the offline analysis of the Sony **ZV-E10** (firmware 2.03, the
current public updater for the body) and everything needed to plan — but not to run —
a port of the a6400's RAM-only touch mod (`MenuTouchFull`) to that body.

> The ZV-E10 is the **same BIONZ-X generation** as the a6400 (CXD90045), so the
> framework is shared and most analysis methods carry over. The **DSC-HX50V** is a
> different BIONZ generation (ARMv6) and lives in the separate `experimental-hx50v`
> branch.

## Contents

| File | What it is |
|---|---|
| `docs/porting/zv-e10/zv-e10-firmware-dump.md` | Firmware dump analysis: official updater package unpack, on-device flash dump, kernel identity, live-probe transcript, artifacts list |
| `docs/porting/zv-e10/zv-e10-touch-mod-blueprint.md` | Cross-reference: every a6400 absolute address → ZV-E10 equivalent, with confidence levels; effort estimate; what the live phase would require |
| `docs/porting/zv-e10/zv-e10-viewUnified2-RE-report.md` | Full RE of `viewUnified2.so` (14 MB): `ViewSettingMenu` vtable, 148-case TBH Action dispatcher, `Action1`, move handlers, `PAS_MenuListTab` vtable |
| `docs/porting/zv-e10/zv-e10-libObj-RE-report.md` | Full RE of `libObj.so` (21 MB): `ViewBase::Action/Main`, DefRootView::ActionCheckerListBase, `EventManager::push`, `Event` ctor, `g_pEvtMgr`, `WidgetSystem::sys_getInstance` |
| `scripts/porting/zv_e10_boot.py` | Boots the extracted ZV-E10 updater kernel in the Unicorn CXD90045 machine (E0510-style); offline |
| `scripts/porting/extract_ext4.py` | Extract ext2/3/4 Sony partition images with 1024-byte boot record via fwtool |
| `scripts/porting/read_fat.py` | List Sony FAT12/16 partition contents via fwtool |

## How this work was done (safety)

- The firmware was obtained two ways: (1) the official Sony public firmware updater
  (analyzed offline, never executed); (2) a **read-only** on-device dump via the
  service shell (`pull /dev/nflasha`, `pull /dev/nflashaB0/B1`), no writes to flash,
  no settings changes.
- All analysis ran on the extracted files on a workstation. **Nothing was written to
  any camera.**
- Any future live work requires a separately reviewed, explicitly authorized
  per-body procedure (see the a6400 evidence for why the ILCE-6400 debugger does not
  transfer).

## What is deliberately NOT in this branch

- Firmware images, flash dumps, bootloaders, updater executables (proprietary)
- Sony service manuals (copyrighted; cited, not redistributed)
- Kernel `kallsyms` dumps (derived artifacts)
- Any a6400-private artifacts beyond what upstream already publishes

## Known limitations

- `libObj.so`'s 16,254-byte RX zero run is in `.rodata`, not `.text` — the loader
  injection home needs a new approach before the mod can be installed
- The ZV-E10's vlog-focused menu layout may not accommodate a 7th "Mods" tab design
- Load bases (runtime) are not knowable offline; every offset here is `base + file`
