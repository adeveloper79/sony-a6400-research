"""Experimental: boot DSC-HX50V updater kernel on the a6400's Unicorn machine.

DSC-HX50V is ARMv6 (Sony CE Linux 2.6.29.6-rt24 SMP, no VFP) — a different BIONZ
generation from the a6400's ARMv7 CXD90045. This does NOT boot to a shell; it
reaches MMU-on VA-mode execution through __mmap_switched and stops in the
low-memory/vector handoff, the same wall the a6400 spent months on.

Usage:
    HX50V_EXTRACT=/path/to/nflasha_unpacked/nflasha1_unpacked python scripts/porting/hx50v_boot.py [instruction_limit]

Offline only; the firmware extraction is not part of this repository (proprietary).
See docs/porting/hx50v/hx50v-firmware-dump.md for how it was produced.
"""
import json
import os
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO))

from src.a6400.execution.arm_atags import build_atags
from src.a6400.execution.unicorn_machine import UnicornMachine

EXTRACT = Path(os.environ.get(
    'HX50V_EXTRACT',
    REPO / 'scratch' / 'nflasha_unpacked' / 'nflasha1_unpacked'))
OUT = Path(os.environ.get('HX50V_BOOT_OUT', REPO / 'artifacts' / 'hx50v_boot'))
OUT.mkdir(parents=True, exist_ok=True)


class Hx50vUpdaterMachine(UnicornMachine):
    PAGE_OFFSET = 0xC0000000
    LOWMEM_DELTA = 0x40000000  # VA stext 0xc0018000 <-> PA 0x80018000

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        from unicorn import UC_HOOK_CODE
        from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2
        uc = self.uc
        self.hx_printk_trace = []

        def printk_hook(uc_, address, size, user):
            va = uc_.reg_read(UC_ARM_REG_R0)
            pa = va - self.LOWMEM_DELTA if va >= self.PAGE_OFFSET else va
            text = ''
            try:
                raw = bytes(uc_.mem_read(pa, 200))
                text = raw.split(b'\0', 1)[0].decode('latin1', 'replace')
            except Exception:
                text = '<unreadable va=0x%08x pa=0x%08x>' % (va, pa)
            self.hx_printk_trace.append({'pc': address, 'format': text,
                                         'r1': uc_.reg_read(UC_ARM_REG_R1),
                                         'r2': uc_.reg_read(UC_ARM_REG_R2)})

        uc.hook_add(UC_HOOK_CODE, printk_hook, begin=0xc014ae64, end=0xc014ae64 + 3)

    def _translate_virtual(self, vaddr: int):
        if vaddr >= 0xFFFF0000:
            return vaddr            # high vectors / SRAM window
        if vaddr >= 0xF0000000:
            return vaddr            # MMIO window identity
        if 0x00000000 <= vaddr < 0x10000:
            return vaddr            # low vectors / scratch
        if 0x80000000 <= vaddr < 0xC0000000:
            return vaddr            # low RAM / aperture identity
        if vaddr >= self.PAGE_OFFSET:
            pa = vaddr - self.LOWMEM_DELTA
            if 0x80000000 <= pa < 0xA0000000:
                return pa
            return None
        if not self._mmu_enabled:
            return vaddr            # pre-MMU identity
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
    raw = (EXTRACT / 'boot' / 'vmlinux.raw').read_bytes()
    kernel = bytearray(raw[8:])
    # PROC record at VA 0xc0028548 (kallsyms) -> file offset 0x10540 in the
    # stripped image; zero cpuid+mask (words 0 and 1) so the lookup passes.
    kernel[0x10540:0x10548] = b'\0' * 8
    kernel_load = 0x80018000
    print('updater kernel:', len(kernel), 'bytes, load PA 0x80018000')

    initrd = (EXTRACT / 'boot' / 'initrd.img').read_bytes()
    initrd_load = 0x80408000
    cmdline = ('root=/dev/ram0 initrd=0x%x,%d init=/sbin/init rdinit=/sbin/init '
               'console=ttyAMA0 mem=80M@0x80000000' % (initrd_load, len(initrd)))
    print('cmdline:', cmdline)

    atags = build_atags(cmdline.encode('latin1'), [(0x80000000, 80 * 1024 * 1024)],
                        boot_params=0xF2000000)
    (OUT / 'atags.bin').write_bytes(atags)

    machine = Hx50vUpdaterMachine(
        bytes(kernel), initrd, atags,
        machine_id=0x3c80,
        kernel_load=kernel_load,
        initrd_load=initrd_load,
        boot_params=0xF2000000,
        ram_banks=((0x00000000, 0x40000000), (0x80000000, 0x40000000)),
        mmio_ranges=((0xF0000000, 0xFDFFFFFF), (0xFE100000, 0xFFFFFFFF)),
        timer_irq_model=False,
        timer_irq=163,
        unmapped_mmio_policy='observe-zero',
        virtual_tlb_enabled=True,
        virtual_tlb_on_userspace=False,
        data_abort_delivery=True,
        instruction_limit=int(sys.argv[1]) if len(sys.argv) > 1 else 60_000_000,
        serial_input=b'',
        uart_base=0xF2038000,
    )
    report = machine.run()
    (OUT / 'boot-report.json').write_text(json.dumps(report, indent=2, default=str))

    print()
    print('stop_reason:', report.get('stop_reason'))
    print('stop_pc:', hex(report.get('stop_pc') or 0))
    print('exception_state:', report.get('exception_state'))
    print('ttbr0_write_hits:', report.get('ttbr0_write_hits'))
    print('tlb_fills:', report.get('tlb_fills'))
    print('data_aborts_delivered:', report.get('data_aborts_delivered'))
    print('printk lines:')
    for e in machine.hx_printk_trace:
        print('   %s' % e['format'])
    print('serial_text:', repr(report.get('serial_text'))[:500])


if __name__ == '__main__':
    main()
