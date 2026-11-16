"""Boot the ZV-E10 updater kernel (nflasha1) on the CXD90045 Unicorn machine.

Offline experiment — boots the extracted updater kernel, never touches a camera.
Modelled on tools/research/run_nflasha1_boot.py (E0510), but sources the boot set
from a local firmware extraction and patches the baked-in init= token in RAM
(symbol-free; the updater kernel is stripped).

Usage:
    ZV_E10_EXTRACT=/path/to/extracted/updater python scripts/porting/zv_e10_boot.py [instruction_limit]

The extraction is not part of this repository (firmware is proprietary); see
docs/porting/zv-e10/zv-e10-firmware-dump.md for how it was produced.
"""
import json
import os
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO))

from src.a6400.execution.machine_contract import UpdaterBootContract
from src.a6400.execution.nflasha_boot_set import NflashaBootSet
from src.a6400.execution.unicorn_machine import UnicornMachine
from src.a6400.targets import get_profile
from tools.research.decode_kernel_kallsyms import decode

EXTRACT = Path(os.environ.get(
    'ZV_E10_EXTRACT',
    REPO / 'scratch' / 'zv-e10-v203-unpacked' / 'firmware.tar_unpacked' / '0400_updater' / 'dev' / 'nflasha1'))
OUT = Path(os.environ.get('ZV_E10_BOOT_OUT', REPO / 'artifacts' / 'zv_e10_boot'))
OUT.mkdir(parents=True, exist_ok=True)


def main():
    image = EXTRACT.read_bytes()
    import hashlib
    identity = {'declared_size': len(image),
                'sha256': hashlib.sha256(image).hexdigest()}
    boot_set = NflashaBootSet('nflasha1', image, identity)
    print('boot set files:', sorted(boot_set.files))

    cmdline_extra = 'init=/bin/ash console=ttyAM0,115200n8 amba2.console=1'
    contract = UpdaterBootContract(boot_set, cmdline_extra=cmdline_extra)
    verification = contract.verify()
    print('machine:', verification['machine'])
    print('load:', verification['load_addresses'])
    print('checks:', json.dumps(verification['checks'], indent=2))

    # symbol-free patch: rewrite the baked-in CONFIG_CMDLINE token in the RAM image
    raw_kernel = bytearray(contract.raw_kernel)
    needle = b'init=/sbin/init'
    replacement = b'init=/bin/ash\x00\x00\x00'
    start = 0
    while True:
        index = raw_kernel.find(needle, start)
        if index < 0:
            break
        window = raw_kernel[max(0, index - 96):index + 96]
        if b'root=/dev/ram0' in window:
            print('patching CONFIG_CMDLINE at file offset 0x%x' % index)
            raw_kernel[index:index + 16] = replacement
            break
        start = index + 1
    else:
        print('WARNING: CONFIG_CMDLINE init= token not found; leaving unpatched')

    symbols = {}
    try:
        decoded = decode(contract.raw_kernel)
        symbols = {s['name']: s['address'] for s in decoded['symbols']}
        print('symbols:', len(symbols))
    except Exception as e:
        print('symbol decode unavailable (expected: updater kernel is stripped):', e)

    target_profile = get_profile('ilce_6400')  # ZV-E10 runs the same CXD90045 machine

    machine = UnicornMachine.from_target(
        target_profile,
        bytes(raw_kernel), contract.initrd, contract.atags,
        machine_id=contract.machine.machine_id,
        kernel_load=contract.load_addresses['kernel_load'],
        initrd_load=contract.load_addresses['initrd_load'],
        boot_params=contract.load_addresses['boot_params'],
        mmio_read_stubs=None,
        boot_patches={
            # VFP D16 check: bne (skip BUG) -> b (always skip BUG). labelled bring-up patch.
            0x60421f00: [(0x00421fd8, 4, 0xEA000005)],
        },
        timer_irq_model=True,
        timer_ticks_per_instruction=1,
        timer_ticks_per_jiffy=0x2710,
        timer_irq=153,
        unmapped_mmio_policy='observe-zero',
        exclusive_fallback=False,
        data_abort_fsr=0x5,
        virtual_tlb_enabled=True,
        virtual_tlb_on_userspace=True,
        enable_svc_dispatch=True,
        data_abort_delivery=True,
        uart_base=0xF2000000,
        serial_input=b'uname -a\nid\nls -la /\n',
        trace_user_mode=True,
        boot_params_payload=contract.sony_payload,
        instruction_limit=int(sys.argv[1]) if len(sys.argv) > 1 else 800_000_000,
        symbols=symbols)
    report = machine.run()
    (OUT / 'boot-report.json').write_text(json.dumps(report, indent=2, default=str),
                                          encoding='utf-8')
    (OUT / 'serial.log').write_text(report.get('serial_text', ''), encoding='latin1')

    print()
    print('stop:', report.get('stop_reason'), 'pc', hex(report.get('stop_pc') or 0))
    print('stop_symbol:', report.get('stop_symbol'))
    print('serial bytes:', report.get('serial_bytes'))
    print('user_mode_entries:', len(report.get('user_mode_entries') or []))
    print('stubs:', report.get('stub_hits'))
    print('unmapped:', report.get('unmapped', [])[:3])
    print('mmio_reads:', report.get('mmio_reads', [])[:12])
    print('klog lines:', len(report.get('klog_lines', [])))
    print()
    print('=== serial output ===')
    print(report.get('serial_text', ''))


if __name__ == '__main__':
    main()
