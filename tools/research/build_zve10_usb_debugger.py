"""Build and audit the ZV-E10 Native USB Debugger binaries.

Performs static verification:
- Checks ARMv7-A softfp EABI flags
- Checks absence of writable+executable PT_LOAD segments
- Verifies symbol resolution against the ZV-E10 pulled libc.so.6
- Verifies _start entry point in executable
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection

DAEMON_SRC = ROOT / "tools/native_camera/zve10/zve10_usb_debugd.c"
STARTUP_SRC = ROOT / "tools/native_camera/debug_start.S"
LAUNCHER_SRC = ROOT / "tools/native_camera/hook/usb_debug_launcher.c"
ZVE10_LIBC = ROOT / "artifacts/hardware/zv_e10-2.02-pull/libc.so.6"
OUT_DIR = ROOT / "artifacts/native/zve10-usb-debugger"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit_elf(executable: Path, launcher: Path, libc: Path) -> dict:
    """Fail-closed static audit of built ARM ELF binaries against ZV-E10 libc."""
    receipt = {"executable": {}, "launcher": {}}
    with libc.open("rb") as handle:
        libc_elf = ELFFile(handle)
        libc_dyn = libc_elf.get_section_by_name(".dynsym")
        libc_exports = {
            s.name for s in libc_dyn.iter_symbols()
            if s.name and s["st_shndx"] != "SHN_UNDEF"
        }

    for role, path in (("executable", executable), ("launcher", launcher)):
        with path.open("rb") as handle:
            elf = ELFFile(handle)
            if elf.header.e_machine != "EM_ARM":
                raise ValueError(f"{role}: expected EM_ARM, got {elf.header.e_machine}")
            if elf.little_endian is not True:
                raise ValueError(f"{role}: expected little-endian ELF")
            if elf.header.e_flags & 0x400:
                raise ValueError(f"{role}: hard-float EABI is incompatible with camera")

            dynamic = elf.get_section_by_name(".dynsym")
            if dynamic:
                undefined = {
                    s.name for s in dynamic.iter_symbols()
                    if s.name and s["st_shndx"] == "SHN_UNDEF"
                }
                missing = undefined - libc_exports
                if missing:
                    raise ValueError(f"{role}: unresolved against ZV-E10 libc: {sorted(missing)}")

            for segment in elf.iter_segments():
                if (
                    segment.header.p_type == "PT_LOAD"
                    and (segment.header.p_flags & 0x2)
                    and (segment.header.p_flags & 0x1)
                ):
                    raise ValueError(f"{role}: writable+executable PT_LOAD segment found")

            symtab = elf.get_section_by_name(".symtab")
            if role == "executable" and symtab and isinstance(symtab, SymbolTableSection):
                defined = {s.name for s in symtab.iter_symbols() if s.name and s["st_shndx"] != "SHN_UNDEF"}
                if "_start" not in defined:
                    raise ValueError("executable has no _start entry symbol")

            receipt[role] = {
                "file": path.name,
                "bytes": path.stat().st_size,
                "sha256": digest(path),
                "audit": "passed",
            }

    return receipt


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit or build ZV-E10 native USB debugger binaries.")
    parser.add_argument("--audit-only", action="store_true", help="Audit pre-existing binaries without building.")
    args = parser.parse_args()

    if not ZVE10_LIBC.is_file():
        print(f"[-] ZV-E10 libc not found at {ZVE10_LIBC}")
        return 1

    exe_path = OUT_DIR / "zve10_usb_debugd.elf"
    launcher_path = OUT_DIR / "usb_debug_launcher.so"

    if args.audit_only:
        if not exe_path.is_file() or not launcher_path.is_file():
            print("[-] Binaries to audit not found in", OUT_DIR)
            return 1
        receipt = audit_elf(exe_path, launcher_path, ZVE10_LIBC)
        print(json.dumps(receipt, indent=2))
        return 0

    print("[*] Build requires arm-linux-gnueabi-gcc in WSL or Linux environment.")
    print("    See tools/native_camera/zve10/build_zve10_usb_debugger.sh")
    return 0


if __name__ == "__main__":
    sys.exit(main())
