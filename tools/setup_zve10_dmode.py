import sys
import os
import io

repo_root = r"H:\AI_harness\agy_wrk\sony-a6400-research"
pmca_path = os.path.join(repo_root, "Sony-PMCA-RE")
if pmca_path not in sys.path:
    sys.path.insert(0, pmca_path)

from pmca.commands.usb import senserShellCommand

def setup_dmode(cam):
    print("[+] Connected to ZV-E10 in Senser mode!")
    
    # Write /setting/mode/dmode
    dmode_data = b"3p\n"
    preload_data = b"/setting/usb_debug_launcher.so\n"
    env_data = b"LD_PRELOAD=/setting/usb_debug_launcher.so\n"
    
    print("[*] Writing /setting/mode/dmode...")
    cam.writeFile("/setting/mode/dmode", dmode_data)
    read_dmode = cam.readFile("/setting/mode/dmode")
    print(f"[+] /setting/mode/dmode verified: {read_dmode}")

    print("[*] Writing /setting/mode/preload...")
    cam.writeFile("/setting/mode/preload", preload_data)
    read_preload = cam.readFile("/setting/mode/preload")
    print(f"[+] /setting/mode/preload verified: {read_preload}")

    print("[*] Writing /setting/env.txt...")
    cam.writeFile("/setting/env.txt", env_data)
    read_env = cam.readFile("/setting/env.txt")
    print(f"[+] /setting/env.txt verified: {read_env}")

    print("[+] All debug preload gates successfully configured!")

if __name__ == '__main__':
    senserShellCommand(driverName='libusb', complete=setup_dmode)
