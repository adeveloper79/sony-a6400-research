"""Control script for ZV-E10 Full-Menu Touch Mod (RAM-only).

Communicates via the native Sony USB debugger to stage the guard binary
and .req packets into /tmp/ (RAM tmpfs, zero flash writes), then executes
the guard to perform preflight checks, installation, or removal.
"""
import argparse
import hashlib
import json
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.camera_debugger import connect

HW = ROOT / "artifacts/hardware/zve10"
GUARD_BIN = HW / "native_menu_touch_full_guard"
MANIFEST_PATH = HW / "menu-touch-full-build.json"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "action",
        choices=["check", "install", "remove", "status"],
        help="Action to perform: check (op 0), install (op 1), remove (op 2), status",
    )
    args = parser.parse_args()

    if not GUARD_BIN.is_file():
        raise FileNotFoundError(f"Guard binary not found: {GUARD_BIN}. Run build_zve10_menu_touch.py first.")
    if not MANIFEST_PATH.is_file():
        raise FileNotFoundError(f"Build manifest not found: {MANIFEST_PATH}. Run build_zve10_menu_touch.py first.")

    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))

    if args.action == "status":
        print(f"=== ZV-E10 Menu Touch Mod Status ===")
        print(f"Target: {manifest.get('camera')} (FW {manifest.get('firmware')})")
        print(f"Code address: {manifest.get('code_address')} ({manifest.get('code_bytes')} bytes)")
        print(f"Hook rel: {manifest.get('hook_rel')}, Hook prs: {manifest.get('hook_prs')}")
        with connect() as d:
            res = d.request(3, b"ps | grep im.elf")
            print(f"im.elf status: {res['payload'].decode('utf-8', 'replace').strip()}")
        return

    req_file = HW / f"menu-touch-full-{args.action}.req"
    if not req_file.is_file():
        raise FileNotFoundError(f"Request file not found: {req_file}")

    guard_data = GUARD_BIN.read_bytes()
    req_data = req_file.read_bytes()

    print(f"=== ZV-E10 Menu Touch Mod: {args.action.upper()} ===")
    print(f"Guard binary: {len(guard_data)} bytes (SHA-256: {hashlib.sha256(guard_data).hexdigest()[:16]}...)")
    print(f"Request data: {len(req_data)} bytes (SHA-256: {hashlib.sha256(req_data).hexdigest()[:16]}...)")
    print(f"Connecting to camera via native USB debugger...")

    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    receipt = {
        "timestamp": timestamp,
        "action": args.action,
        "manifest": manifest,
        "guard_bytes": len(guard_data),
        "request_bytes": len(req_data),
    }

    with connect() as d:
        print("[1/3] Uploading guard to RAM (/tmp/native_menu_touch_full_guard)...")
        d.push("/tmp/native_menu_touch_full_guard", guard_data)

        print(f"[2/3] Uploading request to RAM (/tmp/menu-touch-full.req)...")
        d.push("/tmp/menu-touch-full.req", req_data)

        print(f"[3/3] Executing guard in RAM...")
        cmd = b"chmod 755 /tmp/native_menu_touch_full_guard && /tmp/native_menu_touch_full_guard /tmp/menu-touch-full.req"
        result = d.request(3, cmd)

        output = result["payload"].decode("utf-8", "replace")
        receipt["status"] = result["status"]
        receipt["flags"] = result["flags"]
        receipt["log"] = output

        print("\n--- Guard Execution Output ---")
        print(output.strip())
        print("------------------------------\n")

        # Clean up staged files in /tmp (pure RAM)
        d.request(3, b"rm -f /tmp/menu-touch-full.req")

        receipt_path = HW / f"zve10-touch-{args.action}-{timestamp}.json"
        receipt_path.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
        print(f"Audit receipt saved to: {receipt_path}")

        if "END result=0" in output:
            if args.action == "check":
                print("\n[SUCCESS] Preflight check PASSED! Target im.elf memory bounds and zero padding verified.")
            elif args.action == "install":
                print("\n[SUCCESS] MTF2 Touch Mod INSTALLED in RAM! Test physical menu touch on the camera.")
            elif args.action == "remove":
                print("\n[SUCCESS] MTF2 Touch Mod REMOVED from RAM! Original prologues restored.")
        else:
            print("\n[WARNING] Guard finished with non-zero status or unexpected output.")
            sys.exit(1)


if __name__ == "__main__":
    main()
