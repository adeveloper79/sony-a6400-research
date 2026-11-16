from pathlib import Path
from elftools.elf.elffile import ELFFile
import capstone

lib_path = Path(r"H:\AI_harness\deepseek_wrk\zv-e10-flasha15\lib\libObj.so")
elf = ELFFile(open(lib_path, "rb"))

# Find load offset / section containing 0x00618000
for seg in elf.iter_segments():
    vaddr = seg['p_vaddr']
    memsz = seg['p_memsz']
    filesz = seg['p_filesz']
    offset = seg['p_offset']
    if vaddr <= 0x00618000 < vaddr + memsz:
        print(f"Segment: vaddr=0x{vaddr:x}, memsz=0x{memsz:x}, offset=0x{offset:x}")
        file_offset = offset + (0x00618000 - vaddr)
        break

f = open(lib_path, "rb")

def disasm_at(vaddr, count=25):
    file_off = offset + (vaddr - seg['p_vaddr'])
    f.seek(file_off)
    data = f.read(count * 4)
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
    print(f"\n--- Disassembly at 0x{vaddr:08x} ---")
    for ins in md.disasm(data, vaddr):
        print(f"0x{ins.address:08x}: {ins.mnemonic} {ins.op_str}")

disasm_at(0x00618000, 20)
disasm_at(0x0061810c, 20)
disasm_at(0x006172cc, 20)
