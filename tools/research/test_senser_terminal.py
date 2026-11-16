import sys
import os
import time

repo_root = r"H:\AI_harness\agy_wrk\sony-a6400-research"
pmca_path = os.path.join(repo_root, "Sony-PMCA-RE")
if pmca_path not in sys.path:
    sys.path.insert(0, pmca_path)

from pmca.commands.usb import senserShellCommand

def test_terminal(cam):
    print("[+] Connected to Senser!")
    print("[*] Enabling terminal on usbgcon-1...")
    cam.setTerminalEnable(False)
    time.sleep(0.1)
    cam.setTerminalEnable(True)
    time.sleep(0.5)

    print("[*] Reading initial banner...")
    for _ in range(10):
        d = cam.dev.readTerminal()
        if d:
            print(f"RAW RECV: {d!r}")
        time.sleep(0.1)

    print("[*] Sending 'uname -a\\n'...")
    cam.dev.writeTerminal(b"uname -a\n")
    time.sleep(0.3)

    out = b""
    for _ in range(20):
        d = cam.dev.readTerminal()
        if d:
            out += d
            print(f"RECV: {d!r}")
        time.sleep(0.1)

    print(f"\n[+] Total output:\n{out.decode('latin1', errors='replace')}")

    print("[*] Disabling terminal...")
    cam.setTerminalEnable(False)

if __name__ == '__main__':
    senserShellCommand(driverName='libusb', complete=test_terminal)
