"""Experimental: boot the FULL zImage for the DSC-HX50V updater kernel.

The updater image is a self-decompressing ARM zImage that decompresses itself to
PA 0x80018000 (the output-address literal baked into the decompressor) and jumps
to VA stext 0xc0018000. The stripped direct-boot attempt (hx50v_boot.py) reaches
MMU-on VA-mode but stops in the low-memory/vector handoff; this variant runs the
real zImage and reaches the same wall with a cleaner boot path.

Usage:
    HX50V_EXTRACT=/path/to/nflasha_unpacked/nflasha1_unpacked python scripts/porting/hx50v_boot_zimage.py [instruction_limit]

Offline only; the firmware extraction is not part of this repository (proprietary).
See docs/porting/hx50v/hx50v-firmware-dump.md for how it was produced.
"""
import json
import os
import struct
import sys
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO))

from src.a6400.execution.arm_atags import build_atags
from src.a6400.execution.unicorn_machine import UnicornMachine

EXTRACT = Path(os.environ.get(
    'HX50V_EXTRACT',
    REPO / 'scratch' / 'nflasha_unpacked' / 'nflasha1_unpacked'))
OUT = Path(os.environ.get('HX50V_BOOT_OUT', REPO / 'artifacts' / 'hx50v_boot_zimage'))
OUT.mkdir(parents=True, exist_ok=True)


class Hx50vUpdaterMachine(UnicornMachine):
    PAGE_OFFSET = 0xC0000000
    LOWMEM_DELTA = 0x40000000  # VA stext 0xc0018000 <-> PA 0x80018000

    def _translate_virtual(self, vaddr: int):
        if vaddr >= 0xFFFF0000:
            return vaddr
        if vaddr >= 0xF0000000:
            return vaddr
        if 0x00000000 <= vaddr < 0x10000:
            return vaddr  # low vectors / scratch
        if 0x80000000 <= vaddr < 0xC0000000:
            return vaddr            # low RAM / aperture identity
        if vaddr >= self.PAGE_OFFSET:
            pa = vaddr - self.LOWMEM_DELTA
            if 0x80000000 <= pa < 0xA0000000:
                return pa
            return None
        if not self._mmu_enabled:
            return vaddr
        return self._walk_page_tables_phys(vaddr)

    def _on_tlb_fill(self, uc, vaddr, access, entry, user_data=None):
        self.tlb_fills += 1
        try:
            if access == 18 and vaddr >= self.PAGE_OFFSET:
                self._mmu_enabled = True
            target = self._translate_virtual(vaddr)
            if target is None:
                self.tlb_fill_misses += 1
                return False
            entry.paddr = target
            entry.perms = 7
            return True
        except Exception:
            return False


def main():
    zimage = (EXTRACT / 'boot' / 'vmlinux').read_bytes()
    initrd = (EXTRACT / 'boot' / 'initrd.img').read_bytes()
    print('zImage:', len(zimage), 'initrd:', len(initrd))
    raw_decompressed = zlib.decompress(zimage[13196:], 16 + zlib.MAX_WBITS)[8:]  # head-word aligned
    print('decompressed stub:', len(raw_decompressed), 'bytes')
    cmdline = ('root=/dev/ram0 initrd=0x80408000,%d init=/sbin/init rdinit=/sbin/init '
               'console=ttyAMA0 mem=80M@0x80000000' % len(initrd))
    print('cmdline:', cmdline)
    atags = build_atags(cmdline.encode('latin1'), [(0x80000000, 80 * 1024 * 1024)],
                        boot_params=0xF2000000)
    (OUT / 'atags.bin').write_bytes(atags)

    machine = Hx50vUpdaterMachine(
        raw_decompressed, initrd, atags,
        machine_id=0x3c80,
        kernel_load=0x801C4340,   # zImage splat site above the output area; machine jumps here
        initrd_load=0x80408000,
        boot_params=0xF2000000,
        ram_banks=((0x00000000, 0x40000000), (0x80000000, 0x40000000)),
        mmio_ranges=((0xF0000000, 0xFDFFFFFF), (0xFE100000, 0xFFFFFFFF)),
        timer_irq_model=False,
        timer_irq=163,
        unmapped_mmio_policy='observe-zero',
        virtual_tlb_enabled=True,
        virtual_tlb_on_userspace=False,
        data_abort_delivery=True,
        instruction_limit=int(sys.argv[1]) if len(sys.argv) > 1 else 400_000_000,
        serial_input=b'',
        uart_base=0xF2038000,
        boot_patches={0x80018000: [(0x80028548, 8, 0)]},  # PROC record zeroed at kernel start
        trace_printk_at=0xc014ae64,   # updater kernel printk (kallsyms)
    )
    # splat the real zImage above the output area
    zimage_load = 0x801C4340
    machine.uc.mem_write(zimage_load, zimage)
    from unicorn.arm_const import UC_ARM_REG_PC
    machine.uc.reg_write(UC_ARM_REG_PC, zimage_load)

    report = machine.run()
    (OUT / 'boot-report.json').write_text(json.dumps(report, indent=2, default=str))
    print('stop_reason:', report.get('stop_reason'))
    print('stop_pc:', hex(report.get('stop_pc') or 0))
    print('exception_state:', report.get('exception_state'))
    print('ttbr0_write_hits:', report.get('ttbr0_write_hits'))
    print('tlb_fills:', report.get('tlb_fills'))
    print('mmio_reads:', [(hex(x['address']), x['count']) for x in report.get('mmio_reads', [])][:15])
    print('mmio_write_count:', report.get('mmio_write_count'))
    print('printk_trace:', report.get('printk_trace'))
    print('user_mode_entries:', len(report.get('user_mode_entries') or []))
    print('serial_text:', repr(report.get('serial_text'))[:500])


if __name__ == '__main__':
    main()
