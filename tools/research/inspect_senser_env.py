import sys
import os
import time

repo_root = r"H:\AI_harness\agy_wrk\sony-a6400-research"
pmca_path = os.path.join(repo_root, "Sony-PMCA-RE")
if pmca_path not in sys.path:
    sys.path.insert(0, pmca_path)

from pmca.commands.usb import senserShellCommand

def run_command(cam, cmd):
    # drain
    while True:
        d = cam.dev.readTerminal()
        if not d:
            break
    cam.dev.writeTerminal((cmd + "\n").encode('latin1'))
    time.sleep(0.4)
    out = b""
    for _ in range(40):
        d = cam.dev.readTerminal()
        if d:
            out += d
            if b"/usr/share/app # " in out or b"# " in out:
                # wait a little more for tail
                time.sleep(0.1)
                d2 = cam.dev.readTerminal()
                if d2:
                    out += d2
                break
        time.sleep(0.05)
    return out.decode('latin1', errors='replace')

def inspect_env(cam):
    print("[+] Connected to Senser interface!")
    cam.setTerminalEnable(False)
    time.sleep(0.1)
    cam.setTerminalEnable(True)
    time.sleep(0.5)

    # Initial drain
    for _ in range(10):
        cam.dev.readTerminal()
        time.sleep(0.05)

    print("\n--- [1] ps | grep im.elf ---")
    print(run_command(cam, "ps | grep im.elf"))

    print("\n--- [2] cat /proc/uptime ---")
    print(run_command(cam, "cat /proc/uptime"))

    print("\n--- [3] df -h ---")
    print(run_command(cam, "df -h"))

    print("\n--- [4] ls -la /tmp ---")
    print(run_command(cam, "ls -la /tmp"))

    cam.setTerminalEnable(False)
    print("\n[+] Inspection complete.")

if __name__ == '__main__':
    senserShellCommand(driverName='libusb', complete=inspect_env)
