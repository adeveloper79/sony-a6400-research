# DSC-HX50V — Offline Firmware Emulation Research Notes

**Date:** 2026-11-13 · **Camera:** Sony DSC-HX50V (serial <serial>), Japan-region unit · **Host:** Windows, DSH workspace
**Inputs:** `nflasha` (256 MB read-only dump), `boot1` (120 KB bootloader dump), `Backup_DSC-HX50V_<serial>_20261113_221611.bin` (settings backup), all in `<workspace>`.
**Rules:** offline work only; no camera writes, no flash/firmware replacement, nothing committed to any upstream repository. The one live interaction (a read-only updatershell `cat` of `/proc` files) is documented below and left the camera in its original mode.

---

## 1. What the camera dump contains

`nflasha` = SDM **partition table** (`8246` magic, version `1.00`, 30 table entries, 15 real partitions). Extract with `fwtool unpack -f nflasha -o out` (fwtool can't handle the updater `.dat`, but handles the raw dump directly):

| Partition | Type | Contents |
|---|---|---|
| nflasha1 (4 MB) | FAT (mkdosfs) | `boot/vmlinux` (updater kernel, zImage), `boot/initrd.img` (2.5 MB cramfs rootfs, busybox `ash`), `boot/loadaddr.txt`, `p1chksum` |
| nflasha2 (3 MB) | FAT | `Backup.bin` (== `Backup.bak`, byte-identical), `DmmConfig.bin`, `kemco.txt` (boot cmdline), `ulogio.bin`, `updater/` |
| nflasha3 (11 MB) | TPZL lzpt | main kernel `vmlinux.bin` (2.75 MB, raw ARM), `initrd.img`, `initrd.img_unpacked/` (2nd-stage rootfs), `av-cam.bin` (BIONZ AV firmware), `bonobo.bin`, `sabin/` |
| nflasha4 (10 MB) | raw | all `0xFF` (erased) |
| nflasha5 (40 MB) | **WBI1** warm-boot image | 150+ sections named by physical address (`0x80000000.dat` …) — the normal-boot RAM image |
| nflasha6 (5 MB) | FAT12 | deleted factory-test files (`DIAC.CSV`-style, `.JIT`, `ibjit.so`) — unrecoverable except fragments |
| nflasha7 (20 MB) | FAT | filesystem |
| nflasha8 (50 MB) | TPZL lzpt | **system rootfs**: full camera Linux userland (`bin glib kmod lib local sabin scenario sbin share tool wlan`), all UI strings (`share/app/string_*.uxc` ×35 languages), fonts, DLNA XMLs |
| nflasha12–15 | raw | all `0x00` (unused) |
| nflasha9–11, 16–30 | — | `flag=0x0` / unused table entries |

`boot1` (`EXBL` bootloader) unpacks to `Loader2.bin` (v31.20.87 @ `0xA01F0000`), `DiagUni.bin`, `MIS.bin`.

**The user's settings Backup (`Backup_DSC-HX50V_…221611.bin`)** is a Sony "Backup.bin" (BK2 format): model `DSC-HX50V`, firmware `23151_01.2013032801`, region `CX63471_E32`, serial `<serial>`, protection **enabled** (`id1 = 1`), 15,287 readable properties, checksum valid, and **byte-different from the camera's internal `nflasha2:Backup.bin`** (though identical in the properties pmca can name — same language mask: 15/35 languages enabled).

### The RAW question (separate deliverable, answered)

DSC-HX50V is JPEG-only in Sony's spec, but the firmware contains a complete RAW pipeline (imaging `.bin`s write `DCIM/100MSDCF/DSC*.ARW`; `libmprctrl.so` has the `JPG/JPG_RAW/RAW` enum; playback handles `DCF_FILE_TYPE_RAW`; all 35 languages ship `RAW & JPEG`/`RAW/JPEG` strings and a `view_kSettingStillFormat.uxc` page with RAW rows). The gate is the **model capability decision in native code** — not a `Backup.bin` property, so pmca's tweaks don't expose it. Enabling it would mean patching/signing custom firmware (brick risk, especially on this Japan-locked unit). No public hack exists. Verdict: present but disabled at the model-config level; not user-accessible.

## 2. Platform identity (from banner + live `/proc`)

| | |
|---|---|
| SoC | **CXD4132** ("Diadem", first-gen BIONZ) — not BIONZ X (CXD90045) |
| Kernel | **2.6.29.6-rt24-alp_nl SMP PREEMPT RT** (Sony CE Linux 6.0.0.4, gcc 4.3.3) |
| updater build | `0000126241@build-f-065`, `#2 SMP PREEMPT RT Wed Sep 26 17:52:57 JST 2012` (matches live `/proc/version`) |
| main build | `jp10239@build-ex-27.cv.sony.co.jp`, `#1 SMP PREEMPT RT Thu Mar 28 04:07:25 JST 2013` |
| Entry | zImage (self-decompressing, magic `0x016F2818` at `+0x24`, payload at `0x338c`; 8-byte trampoline `b #0x8`/`b` before headword `0xE321F0D3`) |
| ARM | **ARMv6K**, ARM11 MPCore-class (`rev 5 (v6l)`), **3 CPUs** SMP, Jazelle (`java`), `swp half thumb fastmult edsp`, **no VFP**, BogoMIPS 124.51, implementer `0x41`, part `0xb02`, architecture field `7` |
| UART | PL011-compatible `cxd4132-uart`: **UART0 `0xF2038000`** (irq **160**), UART1 `0xF203A000` |
| Interrupt controller | **GIC** (all `/proc/interrupts` lines prefixed `GIC`): uart 160 · `CXD4115 timer` 163 · hwtimer0/1/2 167/166/165 · meno 180 · ldec 181 · i2c 195 · ms0 201 · usb_scd.0 222 · L2 235 · IPI/LOC = SMP SGIs/PPIs |
| Memory | RAM `0x80000000` (+80 MB); kernel text `0x80030000–0x801a5fff`, data to `0x801fdf23`; `nand` `0x00000000-0x0001ffff`; MMIO `0xf0000000`+; `uio_pdrv.0` at `0xf5060000`; updater sees 38.5 MB RAM |
| PHYS/VA offset | updater: VA `0xC0000000` → PA `0x80018000` (stext VA `0xc0018000` ↔ PA `0x80030000`); main: same VA, delta consistent with the same formula |
| Machine numbers | updater kernel = **`0x3c80` (15488)**, main kernel = **`0x085a` (2138)** |
| Machine name | `ARM-CXD4132`; machine-desc records (13 words, stride `0x34`) at `0xc002857c` (updater) / `0xc002d858` (main) |

## 3. Live `/proc` snapshot (updater shell, read-only, `cat`)

Commands used: `shell cat /proc/{version,cpuinfo,iomem,interrupts,meminfo,mounts}` then `exit`. Output: `<workspace>\probe_log.txt`. Key points:

- `Memory`: `80000000-9fffffff : System RAM`; `Kernel text 80030000` / `Kernel data 801a6000-801fdf23` — **the updater loads at PA `0x80030000`**
- `interrupts`: three CPUs in every row (`CPU0 CPU1 CPU2`), timer `163` servicing CPU1/2, uart `160` with 4,342 hits on CPU0, `usb_scd.0` 1,253
- `mounts`: `/dev/loop0` bodyfs cramfs, `/dev/nflasha2 /setting vfat rw,sync` — normal mount layout; confirms the updater is the same image flow as production
- fwo/console: `console=ttynull` in `kemco.txt`; updater likely silent on screen (our `console=ttyAMA0` guess produced no serial logs — correct console name/driver untested)

## 4. Emulator status (sony-a6400-research, offline)

Toolchain: clone `ivica00/sony-a6400-research`, venv with `unicorn==2.1.4 capstone==5.0.9 pyelftools==0.33 pillow==13.0.0`. Its own test suite runs **1,455/1,489** on a fresh clone (all 34 failures are missing local artifacts of the a6400's hardware dumps, not code defects).

**Milestones reached (offline):**
1. Both kernels decompressed offline; **kallsyms decoded for both** — 11,774 (updater) and 16,418 (main) symbols (`hx50v_kallsyms_updater.json`, `hx50v_kallsyms_vmlinux_bin.json`). Their decoder ported for the `0xC0008000` range.
2. Machine/processor tables decoded for both kernels; ATAGS pointer per machine desc = `0xF2000000` (Sony on-chip region). Machine numbers: updater `0x3c80`, main `0x085a`.
3. Updater kernel executed in the a6400's `UnicornMachine`: proc lookup passed after zeroing the ARMv6 MPCore proc-record cpuid/mask words in the *local kernel copy* (labelled bring-up patch — Unicorn's canned MIDRs match nothing in this kernel's ARMv6 MPCore table).
4. **MMU enabled and VA-mode execution reached** (`TTBR0=0x8001400a`, `DACR=0x1f`, `SCTLR=0x30c5387f`); the kernel runs in VA space through `__mmap_switched` (the data/BSS copy stage) to `VA 0xc0018180`-ish.
5. **Live camera probe completed** (this note §3) — the pieces the a6400 project needed two hardware sessions to learn. The kernel on the live camera runs at `VA 0xc0018000` ↔ PA `0x80030000` (live `/proc/iomem`), updater build `#2 SMP PREEMPT RT Wed Sep 26 2012` — byte-identical banner to our `nflasha1` updater.

**Final emulator status (2026-11-14):** In the full zImage self-decompression boot, the zImage decompresses itself to PA `0x80018000` (literal at zImage offset `0x164`), executes its own decompressor and relocates/boots at VA `0xc0018000` (PA `0x80018000`), enables the MMU (`TTBR0=0x8001400a`), and executes deep into kernel VA space (reaches the `__mmap_switched` data/BSS copy, then the low-memory handoff around VA `0xc0018154` where it dies on an unmapped low-memory access). In the direct boot of the stripped image with the recommended layout — load PA `0x80018000`, `LOWMEM_DELTA=0x40000000`, proc record zeroed at file offset `0x10540` — the kernel runs through decompression, machine/processor lookup, MMU enable, into `__mmap_switched`, and dies at VA `0xc0018154` (`strne fp, [r5], #4`, r5 = `0x0000ffff`) with an unmapped read at VA `0x00000800` (low-memory/vectors window). All configurations reach `__mmap_switched` (the data/BSS copy stage) and die at the same low-memory handoff — the same class of blocker the a6400 project solved by building `UC_TLB_VIRTUAL` + `UC_HOOK_TLB_FILL` from scratch. The kernel has not reached `start_kernel`; the low-vectors/early-memory handoff remains the hard blocker.

**Things that are NOT true:** the HX50V is not running under emulation to userland yet; nothing here changes the camera; no part of this is safe to flash back. No camera write was performed at any point (the one live session was read-only `cat` of `/proc`).

## 5. What would unblock it

1. Port `_translate_virtual`/`_walk_kernel_pt` for this kernel: `PAGE_OFFSET=0xC0000000`, lowmem linear map VA `0xC0000000–0xC4EE8000` → PA `vaddr-0x3FEE8000`; high vectors `0xFFFF0000` identity; everything else walks the kernel's own TTBR0 page tables (classic ARMv6 descriptors: `02`-section / `02`-small-page checks as the a6400's walker does, but XP at **bit 12**, not v7's bit 4, and no Thumb-2).
2. Possibly: a correct console name so the kernel prints its first boot log — the Sony UART driver registers as `ttyAM0`-`ttyAM1` presumably; the silent serial is cosmetic until then.
3. Close to zero extra value from the camera at this point (all live facts already extracted §3; more probing won't fix the TLB walker).

## 6. Schematics (service manual)

**Sony DSC-HX50/HX50V Service Note (Ver. 1.0, 2013.04, 38 pages) — downloaded, in `service_docs/`.** Only known public copy; © Sony. Contents: NOTE/safety, top/bottom views, parts list (SoC marked **`AVY2G2G05E`** — the CXD4132 BGA marking), overall + power block diagrams, frame schematics, SY-1018 board sheets 1–11, board layouts, IC ball map, parts list.

Key points:
- **IC201 = the SoC** ("CPU, CAMERA DSP, AV SIGNAL PROCESS, LENS CONTROL, MODE CONTROL, HDMI PROCESS", sheets 4/11–6/11). BGA marking `AVY2G2G05E`. ~400+ balls; functional groups match the software we see (HDMI TX, DAC_OUT/DAC_IN for AV, SDIO/MMC Wi-Fi, SIO/I2C for sensors, AF/iris/zoom/shutter motor drivers, TPU touch, 12 MHz crystal + 32.768 kHz RTC, 12 MHz `SY-1018` board, `AVSY-1018 BOARD`).
- **Debug/boot console**: LANC (2.5 mm multi-conector, CN712 on SH-1006 flex PCB) pins 6–10 carry `LANC_DC`, `XRESET_REQ`, **`UART_TX/LANC_SIG` (pin 8)**, **`UART_RX/BOOT_IN` (pin 9)**, `AD_JACK_IN` (pin 10). This is Sony's standard "construction service" UART — the same console the updater prints to.
- IC201 pins: **UART0 = G8 (TXD) / F8 (RXD)**, UART1 = G7/F7, UART2 = G6/F6 (+H8/H7 CTS/RTS).
- **JTAG pads** exist: `TDI AA11`, `TCK AF12`, `TMS AD11`, `TDO AF11`, `RTCK AE11`, `nTRST AB11`.
- **`DEBUG_MODE_0/1/2`** = AE12/AD12/AB12 — the boot/debug strap pins.
- **UART0↔ SoC**: `CN712` → flex → SoC `G8/F8` = UART0.
- **LANC 2.5mm jack = debug console**: pin 8 = UART_TX/LANC_SIG, pin 9 = UART_RX/BOOT_IN, pin 10 = AD_JACK_IN, pin 7 = XRESET_REQ, pin 6 = LANC_DC, pin 15 = XPWR_ON.
- **SoC UARTs**: UART0 = G8/F8, UART1 = G7/F7, UART2 = G6/F6 (+CTS/RTS H8/H7).
- **LCD**: 3.0" 24-bit RGB TFT, `CP001` connector; `LCD_R/G/B_D0–D7` + `LCD_HS`/`LCD_VS`/`LCD_DATEN` — no panel model in the note (Sony proprietary).
- **IC201 JTAG**: AA11/AF12/AD11/AF11/AE11/AB11 = TDI/TCK/TMS/TDO/RTCK/nTRST. `DEBUG_MODE_0/1/2` = AE12/AD12/AB12.
- `X201` = 12 MHz silicon oscillator; `X101` = 32.768 kHz tuning-fork (RTC).

## 7. Artifacts in `<workspace>` (added)

- `nflasha_unpacked/` — partition extraction + nested unpack (787 files, ~475 MB)
- `boot1_unpacked/` — Loader2/DiagUni/MIS + `bootfiles.yaml`
- `nflasha5_unpacked/` — WBI warm-boot sections
- `hx50v_kallsyms_updater.json`, `hx50v_kallsyms_vmlinux_bin.json`
- `hx50v_updater_kernel.bin` (decompressed updater kernel), `nflasha_unpacked\nflasha1_unpacked\boot\vmlinux.raw`
- `probe_log.txt`, `probe_cmds.txt` — live read-only probe output
- `hx50v_boot*.py` — boot experiments; reports in `hx50v_boot*\boot-report.json`
- `verify_backup.py`, `diff_backup.py`, `parse_backup.py` — Backup.bin analysis
- `sony-pmca-re/`, `fwtool.py/`, `sony-a6400-research/` — pristine clones + venv (not modified)
- `Backup_DSC-HX50V_<serial>_20261113_221611.bin` — the user's settings backup (intact, read-only verified)
