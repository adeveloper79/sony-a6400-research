import io
from pathlib import Path
from elftools.elf.elffile import ELFFile
import capstone

elf_path = Path(r"H:\AI_harness\agy_wrk\sony-a6400-research\artifacts\hardware\zve10\menu_touch_full.elf")
elf = ELFFile(open(elf_path, "rb"))
text = elf.get_section_by_name(".text").data()
symtab = elf.get_section_by_name(".symtab")

print("=== SYMBOLS ===")
for s in symtab.iter_symbols():
    if s.name:
        print(f"{s.name}: 0x{s['st_value']:08x}")

print("\n=== FIRST 100 BYTES DISASSEMBLY (.text) ===")
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
for ins in md.disasm(text[:100], 0x011964f0):
    print(f"0x{ins.address:08x}: {ins.mnemonic} {ins.op_str}")
