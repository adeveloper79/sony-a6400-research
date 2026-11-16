import sys
import os
import io

repo_root = r"H:\AI_harness\agy_wrk\sony-a6400-research"
pmca_path = os.path.join(repo_root, "Sony-PMCA-RE")
if pmca_path not in sys.path:
    sys.path.insert(0, pmca_path)

from pmca.commands.usb import senserShellCommand

def inspect(cam):
    print("[+] Connected to ZV-E10!")
    for target in ['/lib/ld-linux.so.3', '/lib/ld-2.11.2.so', '/usr/bin/bootin.elf']:
        try:
            buf = io.BytesIO()
            cam.readFile(target, buf)
            data = buf.getvalue()
            local_path = os.path.join(repo_root, "artifacts", "hardware", os.path.basename(target))
            with open(local_path, "wb") as f:
                f.write(data)
            print(f"[+] Downloaded {target} ({len(data)} bytes) to {local_path}")
        except Exception as e:
            print(f"[-] Failed to read {target}: {e}")

if __name__ == '__main__':
    senserShellCommand(driverName='libusb', complete=inspect)
