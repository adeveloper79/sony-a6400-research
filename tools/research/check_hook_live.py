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

def check_hook_bytes(cam):
    cam.setTerminalEnable(False)
    time.sleep(0.1)
    cam.setTerminalEnable(True)
    time.sleep(0.4)
    for _ in range(10):
        cam.dev.readTerminal()
        time.sleep(0.05)

    # 0x06592000 + 0x00618420 = 0x06BAA420
    # 0x06592000 + 0x00618488 = 0x06BAA488
    # 0x06592000 + 0x011964f0 = 0x077284F0 (RX padding)
    print("\n--- Hook Press (0x06BAA420) ---")
    print(run_cmd(cam, "hexdump -C -n 16 -s 112890912 /proc/157/mem"))

    print("\n--- Hook Release (0x06BAA488) ---")
    print(run_cmd(cam, "hexdump -C -n 16 -s 112891016 /proc/157/mem"))

    print("\n--- RX Zero Padding (0x077284F0) ---")
    print(run_cmd(cam, "hexdump -C -n 32 -s 124945648 /proc/157/mem"))

    cam.setTerminalEnable(False)

if __name__ == '__main__':
    senserShellCommand(driverName='libusb', complete=check_hook_bytes)
