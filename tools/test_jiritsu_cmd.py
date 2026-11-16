import sys
import os
import struct

repo_root = r"H:\AI_harness\agy_wrk\sony-a6400-research"
pmca_path = os.path.join(repo_root, "Sony-PMCA-RE")
if pmca_path not in sys.path:
    sys.path.insert(0, pmca_path)

from pmca.commands.usb import senserShellCommand

def test(cam):
    print("[+] Connected to ZV-E10!")
    path = "/tmp/jiritsu/test.txt"
    p_data = b"\0" + path.encode("ascii") + b"\0"
    resp15 = cam._sendAdjustControlPacket(0x1001, 15, p_data)
    st15 = struct.unpack_from("<I", resp15)[0] if len(resp15) >= 4 else -1
    print(f"[*] cmd 15 status: 0x{st15:08x}, len: {len(resp15)}, raw: {resp15.hex()}")

    body = b"hello\n"
    d_data = b"\0\xff\0" + body
    resp16 = cam._sendAdjustControlPacket(0x1001, 16, d_data)
    st16 = struct.unpack_from("<I", resp16)[0] if len(resp16) >= 4 else -1
    print(f"[*] cmd 16 status: 0x{st16:08x}, len: {len(resp16)}, raw: {resp16.hex()}")

if __name__ == '__main__':
    senserShellCommand(driverName='libusb', complete=test)
