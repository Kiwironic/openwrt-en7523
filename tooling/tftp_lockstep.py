#!/usr/bin/env python3
"""Minimal, robust lockstep TFTP read-only server (RFC 1350, 512-byte blocks).

Designed for vendor U-Boot bootloaders with unreliable TFTP implementations
that degrade after ~58-68 KiB of sustained transfer. Serves 32 KiB chunks
one at a time so the bootloader can load them sequentially into RAM.

Ignores all TFTP options (no OACK) so the client falls back to plain
512-byte block mode. Only a timeout triggers retransmit; duplicate ACKs
are ignored.

Prerequisites:
  sudo sysctl net.ipv4.ip_unprivileged_port_start=68   (or run as root)
  sudo firewall-cmd --add-port=69/udp                    (runtime only, RPM-based)

Usage:
  python3 tftp_lockstep.py                              # defaults below
  python3 tftp_lockstep.py --dir chunks/ --bind 192.168.2.126
  python3 tftp_lockstep.py --dir chunks/ --bind 0.0.0.0 --port 69
"""
import argparse, os, socket, struct, time

DEFAULT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "chunks")
DEFAULT_BIND = "192.168.2.126"
DEFAULT_PORT = 69
BLK = 512
TIMEOUT = 3.0
MAX_RETRANS = 30


def serve(root, bind_addr, bind_port):
    srv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((bind_addr, bind_port))
    print(f"lockstep TFTP on {bind_addr}:{bind_port} root={root}", flush=True)

    while True:
        srv.settimeout(None)
        pkt, client = srv.recvfrom(2048)
        if len(pkt) < 4 or struct.unpack("!H", pkt[:2])[0] != 1:
            continue
        name = os.path.basename(pkt[2:].split(b"\0")[0].decode("ascii", "replace"))
        path = os.path.join(root, name)
        if not os.path.isfile(path):
            srv.sendto(struct.pack("!HH", 5, 1) + b"not found\0", client)
            print(f"RRQ {client} {name}: not found", flush=True)
            continue
        size = os.path.getsize(path)
        print(f"RRQ {client} {name} size={size}", flush=True)
        t0 = time.time()
        ok = True
        with open(path, "rb") as f:
            block = 1
            while ok:
                data = f.read(BLK)
                out = struct.pack("!HH", 3, block & 0xFFFF) + data
                retrans = 0
                srv.sendto(out, client)
                deadline = time.time() + TIMEOUT
                while True:
                    remain = deadline - time.time()
                    if remain <= 0:
                        retrans += 1
                        if retrans > MAX_RETRANS:
                            print(f"  abort at block {block}", flush=True)
                            ok = False
                            break
                        srv.sendto(out, client)
                        deadline = time.time() + TIMEOUT
                        continue
                    srv.settimeout(remain)
                    try:
                        ack, peer = srv.recvfrom(2048)
                    except socket.timeout:
                        continue
                    if peer != client or len(ack) < 4:
                        continue
                    op, ablk = struct.unpack("!HH", ack[:4])
                    if op == 4 and ablk == (block & 0xFFFF):
                        break
                if not ok:
                    break
                if len(data) < BLK:
                    print(f"  DONE {name}: {size} bytes in {time.time()-t0:.1f}s", flush=True)
                    break
                block += 1


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Lockstep RFC-1350 TFTP server for chunked U-Boot loading")
    ap.add_argument("--dir", default=DEFAULT_DIR, help=f"chunk directory (default: {DEFAULT_DIR})")
    ap.add_argument("--bind", default=DEFAULT_BIND, help=f"bind address (default: {DEFAULT_BIND})")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"bind port (default: {DEFAULT_PORT})")
    args = ap.parse_args()
    serve(args.dir, args.bind, args.port)
