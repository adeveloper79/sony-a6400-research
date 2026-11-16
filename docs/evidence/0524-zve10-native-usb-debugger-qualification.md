# 0524: ZV-E10 Native USB Debugger Qualification and Target Integration

Date: 2026-11-16. Scope: **Offline qualification and target integration** using live onboarding
hardware pull `artifacts/onboarding/ZV-E10-FW2.02-20261116-134044.zip`, official ZV-E10 FW 2.03
package, and firmware dump `zv-e10-flasha15`. No camera protocol command, live upload, or
hardware modification was performed during this turn.

## 0. Verdict

**Qualified as a `reference` target in `USB_DEBUGGER_TARGETS` (`target_id="zv_e10"`).**

The ZV-E10 shares the CXD90045 BIONZ X framework with the ILCE-6400 and ILCE-7C, running Sony CE
Linux 8.2.7.6. The USB external command driver pair represents **"same source, different build"**:
- `libusbcmd.so` (15,996 B) is **SHA-256 identical to the ILCE-6400A 1.01 library** (`23b94deb...`).
  Code layout, PT_LOAD segments, dynamic symbols, and the ABI export `PackParamDriver` @ `0x2783`
  (26 B) are identical to the a6400 baseline; the only delta is the addition of one 46-byte `.comment`
  compiler record plus 2 bytes of `e_shoff` alignment padding (+48 B total).
- `usb_extcmd.ko` (14,796 B) is a toolchain generation rebuild of the same source with Sony CE
  Linux 8.2.7.6 (GCC 4.5.1, build 20220614). The -12 byte difference vs the a6400 baseline (14,808 B)
  is confined to two list helpers (`list_finalize` +8 B, `list_delete_node` +4 B in `.text`), exactly
  matching the a6400A 1.01 driver build family (E0506).

The prebuilt daemon (`a6400_usb_debugd.elf`) communicates via raw ioctls on `/dev/usb/usb_extcmd`
and contains zero embedded driver bytes; the launcher (`usb_debug_launcher.so`) is body-agnostic.
ZV-E10 specific source `tools/native_camera/zve10/zve10_usb_debugd.c` and cross-compilation
scripts are provided for offline verification and custom builds.

| Component | Baseline (a6400 / ILCE-7C) | ZV-E10 (FW 2.00–2.03) | Delta Analysis |
| --- | --- | --- | --- |
| `libusbcmd.so` size | 15,948 bytes | 15,996 bytes | +48 bytes (+46 B `.comment` + 2 B `e_shoff` pad) |
| `libusbcmd.so` SHA-256 | `9fbf3339...` | `23b94deb...` | Matches ILCE-6400A 1.01; code is byte-identical |
| `usb_extcmd.ko` size | 14,808 bytes | 14,796 bytes | -12 bytes (`list_finalize` +8, `list_delete_node` +4) |
| `usb_extcmd.ko` normalized SHA | `9d7c97bd...` | `1083cadde...` | Pinned in target spec |
| `usb_extcmd.ko` exact SHA | `fca6bfcf...` / `db7383e9...` | `050ff391...` | Pinned in target spec |
| `libc.so.6` size / SHA | (a6400 glibc 2.11.2) | 984,292 B / `bc93700d...` | Sony CE Linux 8.2.7.6 glibc 2.11.2 from nflasha7 |
| Daemon ABI | Feature 0x23 / Opcode 0x7A | Feature 0x23 / Opcode 0x7A | Identical raw ioctl interface |

## 1. Verified Hardware Pull Fixtures

Extracted from `artifacts/onboarding/ZV-E10-FW2.02-20261116-134044.zip` into
`artifacts/hardware/zv_e10-2.02-pull/`:

```
artifacts/hardware/zv_e10-2.02-pull/
├── camera_identity.json       (model_name: "ZV-E10", model_code: "0032852820", fw: "2.02")
├── compatibility_report.json  (normalized_valid: true, exact hashes recorded)
├── usb_extcmd.ko             (14,796 bytes, SHA: 050ff3913e172f1b98365d88f27c37f0dcd40ed7a6ec014ee8ccc82fba01ad2b)
├── libusbcmd.so              (15,996 bytes, SHA: 23b94deb094d31f160cfe1d46330f2425e94d4801dfccb66720c4cf4b81cfb7e)
├── libc.so.6                 (984,292 bytes, SHA: bc93700df79ac8f67356e4dbf5ab5c4c656865455e18ef2860acc178f5186bc2)
└── ld.so.preload             (0 bytes, unpatched stock camera state)
```

## 2. Compatibility Registry Implementation

In `src/a6400/targets/usb_debugger_compat.py`:

```python
ZVE10_USB_EXTCMD_SIZE = 14796
ZVE10_USB_EXTCMD_NORMALIZED_SHA256 = (
    "1083cadde9dda2534585c1571fc1b320781fdeccb43cfc648e1d342dba55620f"
)
ZVE10_LIBUSBCMD_SIZE = 15996
ZVE10_LIBUSBCMD_SHA256 = (
    "23b94deb094d31f160cfe1d46330f2425e94d4801dfccb66720c4cf4b81cfb7e"
)

USB_DEBUGGER_TARGETS["zv_e10"] = DebuggerTargetSpec(
    target_id="zv_e10",
    model_name="ZV-E10",
    firmware_versions=("2.00", "2.01", "2.02", "2.03"),
    qualification="reference",
    usb_extcmd_size=ZVE10_USB_EXTCMD_SIZE,
    usb_extcmd_normalized_sha256=ZVE10_USB_EXTCMD_NORMALIZED_SHA256,
    usb_extcmd_exact_sha256s=(
        "050ff3913e172f1b98365d88f27c37f0dcd40ed7a6ec014ee8ccc82fba01ad2b",
    ),
    usb_extcmd_reference_paths=(
        "artifacts/hardware/zv_e10-2.02-pull/usb_extcmd.ko",
    ),
    libusbcmd_size=ZVE10_LIBUSBCMD_SIZE,
    libusbcmd_sha256=ZVE10_LIBUSBCMD_SHA256,
    payload_daemon=DEFAULT_PAYLOAD_DAEMON,
    payload_launcher=DEFAULT_PAYLOAD_LAUNCHER,
    notes="Target for Sony ZV-E10 (FW 2.00-2.03, CE Linux 8.2.7.6)...",
)
```

Target lookup support:
- `get_debugger_target("zv_e10")`, `get_debugger_target("ZV-E10")`, `get_debugger_target("zve10")` resolve to `zv_e10`.
- `find_debugger_target_by_model("ZV-E10", "2.02")` matches `zv_e10`.

## 3. Test Verification

Automated test suite execution:
- `tests/test_usb_debugger_compat.py`:
  - `test_registry_lists_qualified_targets`: confirms `zv_e10` and aliases exist.
  - `test_zve10_live_pull_is_compatible_and_rejects_mutations`: verifies live pull against `zv_e10` spec (both with and without local reference file) and verifies single-byte mutations in `libusbcmd.so` and `usb_extcmd.ko` fail closed.
- `tests/test_setup_usb_debugger.py`:
  - `test_find_debugger_target_by_model`: verifies model detection for ZV-E10.
  - `test_zve10_onboarding_bundle_and_developer_unpack`: verifies bundle extraction and target recognition.

All 40 unit tests in the USB debugger suite pass cleanly.
