import sys
import os
import time

repo_root = r"H:\AI_harness\agy_wrk\sony-a6400-research"
pmca_path = os.path.join(repo_root, "Sony-PMCA-RE")
if pmca_path not in sys.path:
    sys.path.insert(0, pmca_path)

from pmca.commands.usb import senserShellCommand

def execute_inspect(cam):
    print("[+] Senser connected")
    cam.setTerminalEnable(False)
    time.sleep(0.1)
    cam.setTerminalEnable(True)
    time.sleep(0.4)
    for _ in range(10):
        cam.dev.readTerminal()
        time.sleep(0.05)

    def cmd(c, timeout=2.0):
        while cam.dev.readTerminal():
            pass
        cam.dev.writeTerminal((c + "\n").encode('latin1'))
        time.sleep(0.2)
        out = b""
        start = time.time()
        while time.time() - start < timeout:
            d = cam.dev.readTerminal()
            if d:
                out += d
                if b"/usr/share/app # " in out or b"# " in out:
                    time.sleep(0.05)
                    d2 = cam.dev.readTerminal()
                    if d2:
                        out += d2
                    break
            time.sleep(0.05)
        return out.decode('latin1', errors='replace')

    print("=== KERNEL DMESG (LOOKING FOR CRASH / PANIC / FAULT) ===")
    print(cmd("dmesg | tail -n 40"))

    print("=== LAST KMSG / LOGS ===")
    print(cmd("ls -la /var/log /tmp"))

    print("=== PROCESS & MAPS ===")
    print(cmd("ps | grep im.elf"))
    print(cmd("grep -E 'libObj|viewUnified2|CautionConfig' /proc/157/maps"))

    cam.setTerminalEnable(False)
    print("[+] Done")

if __name__ == '__main__':
    senserShellCommand(driverName='libusb', complete=execute_inspect)
