import sys
import os
import time

repo_root = r"H:\AI_harness\agy_wrk\sony-a6400-research"
pmca_path = os.path.join(repo_root, "Sony-PMCA-RE")
if pmca_path not in sys.path:
    sys.path.insert(0, pmca_path)

from pmca.commands.usb import senserShellCommand

def run_cmd(cam, cmd):
    while True:
        d = cam.dev.readTerminal()
        if not d:
            break
    cam.dev.writeTerminal((cmd + "\n").encode('latin1'))
    time.sleep(0.3)
    out = b""
    for _ in range(30):
        d = cam.dev.readTerminal()
        if d:
            out += d
            if b"/usr/share/app # " in out:
                break
        time.sleep(0.05)
    return out.decode('latin1', errors='replace')

def check_mounts(cam):
    cam.setTerminalEnable(False)
    time.sleep(0.1)
    cam.setTerminalEnable(True)
    time.sleep(0.4)
    for _ in range(10):
        cam.dev.readTerminal()
        time.sleep(0.05)

    print("\n--- cat /proc/mounts ---")
    print(run_cmd(cam, "cat /proc/mounts"))

    print("\n--- ls -ld /etc ---")
    print(run_cmd(cam, "ls -ld /etc"))

    print("\n--- cat /proc/157/maps | grep -E 'libObj|viewUnified2|CautionConfig' ---")
    print(run_cmd(cam, "cat /proc/157/maps | grep -E 'libObj|viewUnified2|CautionConfig'"))

    cam.setTerminalEnable(False)

if __name__ == '__main__':
    senserShellCommand(driverName='libusb', complete=check_mounts)
