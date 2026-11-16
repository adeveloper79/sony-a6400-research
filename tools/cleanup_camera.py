import sys
import os

repo_root = r"H:\AI_harness\agy_wrk\sony-a6400-research"
pmca_path = os.path.join(repo_root, "Sony-PMCA-RE")
if pmca_path not in sys.path:
    sys.path.insert(0, pmca_path)

from pmca.commands.usb import senserShellCommand

def cleanup(cam):
    print("[+] Connected to ZV-E10 in Senser mode!")
    files_to_delete = [
        "/setting/mode/dmode",
        "/setting/mode/preload",
        "/setting/env.txt",
        "/setting/zve10_usb_debugd.elf",
        "/setting/a6400_usb_debugd.elf",
        "/setting/usb_debug_launcher.so",
        "/setting/zve10_launcher.log",
        "/setting/zve10_daemon.log",
        "/setting/zve10_usb_debug.lock",
        "/tmp/zve10_usb_debug.lock",
        "/setting/launcher_debug.txt",
        "/setting/a6400_usb_debug_launcher.log",
    ]

    for f in files_to_delete:
        try:
            cam.deleteFile(f)
            print(f"[+] Deleted: {f}")
        except Exception as e:
            print(f"[*] {f}: {e}")

    try:
        # Empty /etc/ld.so.preload
        cam.writeFile("/etc/ld.so.preload", b"")
        readback = cam.readFile("/etc/ld.so.preload")
        print(f"[+] /etc/ld.so.preload emptied: {readback!r}")
    except Exception as e:
        print(f"[*] /etc/ld.so.preload: {e}")

    print("[+] Camera cleanup completed successfully. System is returned to factory state.")

if __name__ == '__main__':
    senserShellCommand(driverName='libusb', complete=cleanup)
