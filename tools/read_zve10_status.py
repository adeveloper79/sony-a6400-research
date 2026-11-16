import sys
import os
import io

repo_root = r"H:\AI_harness\agy_wrk\sony-a6400-research"
pmca_path = os.path.join(repo_root, "Sony-PMCA-RE")
if pmca_path not in sys.path:
    sys.path.insert(0, pmca_path)

from pmca.commands.usb import senserShellCommand

def inspect(cam):
    print("[+] Connected to ZV-E10 in Senser mode!")
    paths = [
        "/etc/ld.so.preload",
        "/setting/zve10_launcher.log",
        "/setting/zve10_daemon.log",
        "/tmp/zve10_usb_debug.lock",
        "/setting/zve10_usb_debug.lock",
        "/setting/launcher_debug.txt",
        "/setting/a6400_usb_debug_launcher.log",
        "/setting/env.txt",
        "/setting/mode/preload",
        "/setting/mode/dmode"
    ]
    for p in paths:
        try:
            buf = io.BytesIO()
            cam.readFile(p, buf)
            data = buf.getvalue()
            print(f"=== {p} ({len(data)} bytes) ===")
            if len(data) < 2000:
                print(data.decode("utf-8", errors="replace"))
            else:
                print(data[:500].decode("utf-8", errors="replace") + f"\n... [truncated, total {len(data)} bytes]")
        except Exception as e:
            print(f"=== {p} === NOT FOUND / ERROR: {e}")

if __name__ == "__main__":
    senserShellCommand(driverName="libusb", complete=inspect)
