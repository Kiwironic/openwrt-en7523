#!/usr/bin/env python3
"""Automated load + boot script for EN7523-based routers with vendor U-Boot.

The ONLY script needed for RAM-booting OpenWrt. Run this AFTER power-cycling
the router (or while it's at the U-Boot prompt already). It will:
1. Wait for U-Boot prompt (with rapid interrupts to catch autoboot)
2. Login with credentials from ~/.config/uboot-cred
3. Set network (router=192.168.2.1, server=192.168.2.126)
4. Load all chunks via TFTP (auto-detects count from chunks/ dir)
5. Verify with iminfo (crc32+ sha1+)
6. Boot with procd (normal OpenWrt init — starts dropbear, network, etc.)

Prerequisites:
  - tftp_lockstep.py running on 192.168.2.126:69
  - Host NIC up at 192.168.2.126 (e.g. nmcli con up <your-connection>)
  - sudo sysctl net.ipv4.ip_unprivileged_port_start=68
  - sudo firewall-cmd --add-port=69/udp
  - Chunks in chunks/ (re-chunk after every rebuild)

Credential file (~/.config/uboot-cred):
  First line:  U-Boot username
  Second line: U-Boot password

Usage:
  python3 load_and_boot.py
  python3 load_and_boot.py --port /dev/ttyUSB0 --chunks chunks/
  python3 load_and_boot.py --port /dev/ttyUSB0 --router-ip 192.168.2.1 --server-ip 192.168.2.126
"""
import argparse, glob, os, re, sys, time, serial

DEFAULT_PORT = "/dev/ttyUSB0"
DEFAULT_CHUNK_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "chunks")
DEFAULT_ROUTER_IP = "192.168.2.1"
DEFAULT_SERVER_IP = "192.168.2.126"
DEFAULT_CRED_FILE = "~/.config/uboot-cred"
BASE = 0x8a000000
CHUNK = 0x8000
MAX_RETRY = 5
BOOTARGS = "console=ttyS0,115200n8"


def get_cred(cred_file):
    f = os.path.expanduser(cred_file)
    if os.path.exists(f):
        lines = open(f).read().strip().split('\n')
        if len(lines) >= 2:
            return lines[0].strip(), lines[1].strip()
    return "", ""


def send(ser, cmd):
    ser.write(cmd.encode() + b"\r")
    ser.flush()


def wait_for(ser, patterns, timeout):
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        buf += ser.read(4096)
        for p in patterns:
            m = re.search(p, buf)
            if m:
                return m, buf
    return None, buf


def main():
    ap = argparse.ArgumentParser(description="Automated U-Boot serial + TFTP chunk loader for EN7523 routers")
    ap.add_argument("--port", default=DEFAULT_PORT, help=f"serial port (default: {DEFAULT_PORT})")
    ap.add_argument("--chunks", default=DEFAULT_CHUNK_DIR, help=f"chunk directory (default: {DEFAULT_CHUNK_DIR})")
    ap.add_argument("--router-ip", default=DEFAULT_ROUTER_IP, help=f"router IP in U-Boot (default: {DEFAULT_ROUTER_IP})")
    ap.add_argument("--server-ip", default=DEFAULT_SERVER_IP, help=f"TFTP server IP / host IP (default: {DEFAULT_SERVER_IP})")
    ap.add_argument("--cred", default=DEFAULT_CRED_FILE, help=f"credential file (default: {DEFAULT_CRED_FILE})")
    args = ap.parse_args()

    # === Stage 1: Wait for U-Boot ===
    print("[1/5] Waiting for U-Boot (power-cycle if not already)...", flush=True)
    ser = serial.Serial(args.port, 115200, timeout=0.2)
    ser.reset_input_buffer()

    # Rapidly send interrupts to catch autoboot — send multiple chars per cycle
    # and also check for the countdown prompt to interrupt it
    for i in range(600):  # 120 seconds max
        ser.write(b"\r\r\r")
        time.sleep(0.05)
        data = ser.read(4096)
        if data:
            text = data.decode('ascii', 'replace')
            if 'UserName' in text:
                print("  Got UserName prompt", flush=True)
                break
            if 'ECNT>' in text:
                print("  Got ECNT prompt directly", flush=True)
                break
            if 'autoboot' in text:
                # Send more interrupts immediately to stop the countdown
                ser.write(b"\r\r\r\r\r\r")
                time.sleep(0.1)
                data2 = ser.read(4096)
                if data2:
                    text2 = data2.decode('ascii', 'replace')
                    if 'UserName' in text2 or 'ECNT>' in text2:
                        print("  Interrupted autoboot", flush=True)
                        break
    else:
        print("  FATAL: no U-Boot prompt", flush=True)
        sys.exit(1)

    # === Stage 2: Login ===
    print("[2/5] Logging in...", flush=True)
    user, pwd = get_cred(args.cred)
    if not user:
        print(f"  FATAL: no credentials in {args.cred}", flush=True)
        sys.exit(1)

    # Send username, wait longer for Password prompt (U-Boot can be slow)
    send(ser, user)
    m, buf = wait_for(ser, [rb"Password", rb"ECNT>"], 10)
    if not m:
        # Retry — sometimes the first char gets lost during autoboot interrupt
        send(ser, "\r")
        m, buf = wait_for(ser, [rb"UserName", rb"Password", rb"ECNT>"], 5)
        if m and b"UserName" in m.group(0):
            send(ser, user)
            m, buf = wait_for(ser, [rb"Password", rb"ECNT>"], 10)

    if m and b"Password" in m.group(0):
        send(ser, pwd)
        m, _ = wait_for(ser, [rb"ECNT>"], 10)
        if not m:
            print("  FATAL: login failed (no ECNT> after password)", flush=True)
            sys.exit(1)
        print("  Login OK", flush=True)
    elif m and b"ECNT>" in m.group(0):
        print("  Already logged in (got ECNT> directly)", flush=True)
    else:
        print("  FATAL: no Password or ECNT> prompt after username", flush=True)
        sys.exit(1)

    # === Stage 3: Network ===
    print("[3/5] Setting up network...", flush=True)
    send(ser, f"setenv ipaddr {args.router_ip}")
    wait_for(ser, [rb"ECNT>"], 3)
    send(ser, f"setenv serverip {args.server_ip}")
    wait_for(ser, [rb"ECNT>"], 3)

    # === Stage 4: Load chunks ===
    chunk_files = sorted(glob.glob(os.path.join(args.chunks, "chunk*")))
    if not chunk_files:
        print(f"  FATAL: no chunks found in {args.chunks}", flush=True)
        sys.exit(1)
    N = len(chunk_files)
    LAST_SIZE = os.path.getsize(chunk_files[-1])
    print(f"[4/5] Loading {N} chunks...", flush=True)

    t0 = time.time()
    for i in range(N):
        addr = BASE + i * CHUNK
        expect = LAST_SIZE if i == N - 1 else CHUNK
        name = f"chunk{i:03d}"
        ok = False
        for attempt in range(1, MAX_RETRY + 1):
            ser.reset_input_buffer()
            send(ser, f"tftpboot 0x{addr:x} {name}")
            m, buf = wait_for(ser, [rb"Bytes transferred = (\d+)", rb"Abort"], 90)
            if m and m.group(0) != b"Abort":
                got = int(m.group(1))
                if got == expect:
                    ok = True
                    break
                print(f"  {name}: size mismatch (attempt {attempt})", flush=True)
            else:
                print(f"  {name}: no/abort (attempt {attempt})", flush=True)
            ser.write(b"\x20")
            time.sleep(0.2)
            send(ser, "")
            wait_for(ser, [rb"ECNT>"], 5)
        if not ok:
            print(f"  FATAL: {name} failed", flush=True)
            sys.exit(2)
        if (i + 1) % 50 == 0 or i == N - 1:
            print(f"  [{i+1}/{N}] {name} OK ({time.time()-t0:.0f}s)", flush=True)

    print(f"  ALL {N} CHUNKS LOADED in {time.time()-t0:.0f}s", flush=True)

    # === Stage 5: Verify and boot ===
    print("[5/5] Verifying and booting...", flush=True)
    send(ser, f"iminfo 0x{BASE:x}")
    m, buf = wait_for(ser, [rb"sha1\+", rb"ERROR"], 10)
    if m and b"sha1" in m.group(0):
        print("  Image verified OK", flush=True)
    else:
        print("  WARNING: verification unclear", flush=True)

    send(ser, f"setenv bootargs {BOOTARGS}")
    wait_for(ser, [rb"ECNT>"], 3)
    print("  Booting...", flush=True)
    send(ser, f"bootm 0x{BASE:x}")

    # Capture boot output
    t0 = time.time()
    while time.time() - t0 < 90:
        data = ser.read(4096)
        if data:
            sys.stdout.buffer.write(data)
            sys.stdout.flush()
            if b"~ #" in data or b"root@" in data:
                print("\n  BOOT COMPLETE", flush=True)
                break
        else:
            time.sleep(0.05)

    ser.close()


if __name__ == "__main__":
    main()
