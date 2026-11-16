import sys
import os
import time

repo_root = r"H:\AI_harness\agy_wrk\sony-a6400-research"
pmca_path = os.path.join(repo_root, "Sony-PMCA-RE")
if pmca_path not in sys.path:
    sys.path.insert(0, pmca_path)

from pmca.commands.usb import senserShellCommand

def launch(cam):
    print("[+] Connected to ZV-E10 in Service mode!")
    try:
        print("[*] Authenticating HASP...")
        cam.readHasp()
        print("[+] HASP authenticated!")
    except Exception as e:
        print("[-] HASP notice:", e)

    try:
        print("[*] Enabling service terminal...")
        cam.setTerminalEnable(True)
        print("[+] Service terminal enabled!")
    except Exception as e:
        print("[-] setTerminalEnable notice:", e)

    time.sleep(0.5)

    try:
        # Drain any initial banners
        drained = cam.dev.readTerminal()
        if drained:
            print("[+] Initial terminal output:", repr(drained))

        # Launch the debugger daemon in background
        cmd = b"/setting/zve10_usb_debugd.elf >/setting/zve10_daemon.log 2>&1 &\n"
        print("[*] Sending launch command:", cmd)
        cam.dev.writeTerminal(cmd)
        time.sleep(1.0)

        # Read response
        resp = cam.dev.readTerminal()
        print("[+] Terminal response:", repr(resp))

        # Check ps
        cam.dev.writeTerminal(b"ps\n")
        time.sleep(0.5)
        ps_out = cam.dev.readTerminal()
        print("[+] Running processes:\n", ps_out.decode('latin1', errors='replace') if ps_out else "[no output]")
    except Exception as e:
        print("[-] Terminal execution error:", e)

if __name__ == '__main__':
    senserShellCommand(driverName='libusb', complete=launch)
