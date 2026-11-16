import sys
import os
import time

repo_root = r"H:\AI_harness\agy_wrk\sony-a6400-research"
sys.path.insert(0, os.path.join(repo_root, "Sony-PMCA-RE"))
from pmca.commands.usb import senserShellCommand

def check_vm(cam):
    cam.setTerminalEnable(False)
    time.sleep(0.1)
    cam.setTerminalEnable(True)
    time.sleep(0.4)
    for _ in range(10):
        cam.dev.readTerminal()
        time.sleep(0.05)
    
    # lib_base = 0x06592000. pViewManager = 0x0149221c.
    # 0x06592000 + 0x0149221c = 0x07a2421c
    cam.dev.writeTerminal(b"dd if=/proc/157/mem bs=1 skip=128074268 count=32 2>/dev/null | busybox xxd\n")
    time.sleep(0.5)
    out = b""
    for _ in range(20):
        d = cam.dev.readTerminal()
        if d:
            out += d
        time.sleep(0.05)
    print("Memory at pViewManager (0x07a2421c):")
    print(out.decode("latin1", errors="replace"))
    cam.setTerminalEnable(False)

if __name__ == "__main__":
    senserShellCommand(driverName="libusb", complete=check_vm)
