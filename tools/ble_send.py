#!/usr/bin/env python3
"""Stand-in for the Mac/Android helpers: send now-playing data to the display over BLE.

Example:
  tools/.venv/bin/python tools/ble_send.py --title "Test Song" --artist Artist --album Album \
      --uri spotify:track:abc --duration 225000 --position 30000 --source mac --art cover.jpg --listen 30
"""
import argparse
import asyncio
import io
import struct
import sys
import time

from bleak import BleakClient, BleakScanner

SVC = "6e0a0000-7b5d-4c1e-9f3a-2d8c5b1e4a70"
STATE = "6e0a0001-7b5d-4c1e-9f3a-2d8c5b1e4a70"
META = "6e0a0002-7b5d-4c1e-9f3a-2d8c5b1e4a70"
ART = "6e0a0003-7b5d-4c1e-9f3a-2d8c5b1e4a70"
CMD = "6e0a0004-7b5d-4c1e-9f3a-2d8c5b1e4a70"
INFO = "6e0a0005-7b5d-4c1e-9f3a-2d8c5b1e4a70"
CMD_NAMES = {1: "play_pause", 2: "next", 3: "prev"}
SOURCES = {"unknown": 0, "mac": 1, "phone": 2}


def fnv1a32(s: str) -> int:
    h = 0x811C9DC5
    for b in s.encode("utf-8"):
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def state_bytes(track_id, playing, local, source, position_ms, duration_ms) -> bytes:
    flags = (1 if playing else 0) | (2 if local else 0)
    return struct.pack("<BBBBIII", 1, flags, source, 0, track_id, position_ms, duration_ms)


def meta_bytes(track_id, title, artist, album) -> bytes:
    return struct.pack("<I", track_id) + "\x1f".join([title, artist, album]).encode("utf-8")


def art_jpeg(path: str) -> bytes:
    from PIL import Image

    img = Image.open(path).convert("RGB")
    img.thumbnail((160, 160))
    buf = io.BytesIO()
    img.save(buf, "JPEG", quality=75, progressive=False, optimize=True)
    return buf.getvalue()


async def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--title", default="Test Song")
    ap.add_argument("--artist", default="Artist")
    ap.add_argument("--album", default="Album")
    ap.add_argument("--uri", default="spotify:track:test")
    ap.add_argument("--duration", type=int, default=225000)
    ap.add_argument("--position", type=int, default=0)
    ap.add_argument("--paused", action="store_true")
    ap.add_argument("--source", choices=SOURCES, default="mac")
    ap.add_argument("--not-local", action="store_true")
    ap.add_argument("--art")
    ap.add_argument("--listen", type=float, default=0, help="seconds to stay connected, re-sending State every 5 s")
    ap.add_argument("--timeout", type=float, default=15)
    a = ap.parse_args()

    print(f"scanning for {SVC} ...")
    dev = await BleakScanner.find_device_by_filter(
        lambda d, adv: SVC in [u.lower() for u in adv.service_uuids], timeout=a.timeout
    )
    if dev is None:
        print("display not found", file=sys.stderr)
        return 1
    print(f"found {dev.name or '?'} ({dev.address})")

    tid = fnv1a32(a.uri)
    playing = not a.paused
    started = time.monotonic()

    async with BleakClient(dev) as c:
        art_chr = c.services.get_characteristic(ART)
        chunk = max(20, min(500, art_chr.max_write_without_response_size - 9))
        print(f"connected; mtu={c.mtu_size} art chunk={chunk}")

        def on_cmd(_, data: bytearray):
            print(f"CMD {CMD_NAMES.get(data[0], data[0])}", flush=True)

        await c.start_notify(CMD, on_cmd)
        info = await c.read_gatt_char(INFO)
        print(f"info: protocol v{info[0]} battery={'?' if info[1] == 0xFF else info[1]}")

        await c.write_gatt_char(META, meta_bytes(tid, a.title, a.artist, a.album), response=True)
        if a.art:
            jpg = art_jpeg(a.art)
            await c.write_gatt_char(ART, struct.pack("<BII", 1, tid, len(jpg)), response=True)
            for off in range(0, len(jpg), chunk):
                await c.write_gatt_char(ART, struct.pack("<BII", 2, tid, off) + jpg[off : off + chunk], response=False)
            print(f"art sent: {len(jpg)} bytes")

        def send_state():
            pos = a.position + (int((time.monotonic() - started) * 1000) if playing else 0)
            pos = min(pos, a.duration)
            return c.write_gatt_char(
                STATE, state_bytes(tid, playing, not a.not_local, SOURCES[a.source], pos, a.duration), response=False
            )

        await send_state()
        print("state sent")
        end = time.monotonic() + a.listen
        while time.monotonic() < end:
            await asyncio.sleep(min(5, max(0, end - time.monotonic())))
            if time.monotonic() < end:
                await send_state()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(asyncio.run(main()))
    except KeyboardInterrupt:
        sys.exit(130)
