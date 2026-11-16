import struct
import capstone

v = open(r"H:\AI_harness\deepseek_wrk\zv-e10-flasha15\lib\viewUnified2.so", "rb").read()
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)

matches = [i for i in range(len(v)-4) if v[i:i+4] == b'\xc7\xa2\xdc\xaa']
print(f"Found {len(matches)} matches for 0xaadca2c7:")
for m in matches:
    print(f"\n--- Match at 0x{m:08x} ---")
    # Disassemble 30 bytes before and after if possible
    start = max(0, (m - 32) & ~1)
    for ins in md.disasm(v[start:start+64], start):
        mark = " <--" if ins.address <= m < ins.address + ins.size else ""
        print(f"0x{ins.address:08x}: {ins.mnemonic} {ins.op_str}{mark}")
