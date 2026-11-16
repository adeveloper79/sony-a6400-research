import sys
import os
import io

repo_root = r"H:\AI_harness\agy_wrk\sony-a6400-research"
pmca_path = os.path.join(repo_root, "Sony-PMCA-RE")
if pmca_path not in sys.path:
    sys.path.insert(0, pmca_path)

from pmca.commands.usb import senserShellCommand

def pull_init(cam):
    print("[+] Connected to ZV-E10 in Senser mode!")
    try:
        buf = io.BytesIO()
        cam.readFile("/sbin/init", buf)
        data = buf.getvalue()
        local_path = os.path.join(repo_root, "artifacts", "hardware", "init")
        with open(local_path, "wb") as f:
            f.write(data)
        print(f"[+] Downloaded /sbin/init ({len(data)} bytes) to {local_path}")
    except Exception as e:
        print("[-] Failed to download /sbin/init:", e)

if __name__ == '__main__':
    senserShellCommand(driverName='libusb', complete=pull_init)
