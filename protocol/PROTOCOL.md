# Now-Playing BLE Protocol (v1)

The display is a BLE **peripheral** and accepts up to 3 simultaneous **centrals** (helpers on the Mac or phone). All multi-byte integers are **little-endian**.

Advertised name: `NowPlaying-XXXX`, where XXXX is the last 2 bytes of the display's BLE MAC in hex. The display advertises the service UUID.

## GATT

| Name | UUID | Properties |
|---|---|---|
| Service | `6e0a0000-7b5d-4c1e-9f3a-2d8c5b1e4a70` | |
| State | `6e0a0001-7b5d-4c1e-9f3a-2d8c5b1e4a70` | write without response |
| Meta | `6e0a0002-7b5d-4c1e-9f3a-2d8c5b1e4a70` | write |
| Art | `6e0a0003-7b5d-4c1e-9f3a-2d8c5b1e4a70` | write without response |
| Command | `6e0a0004-7b5d-4c1e-9f3a-2d8c5b1e4a70` | notify |
| Info | `6e0a0005-7b5d-4c1e-9f3a-2d8c5b1e4a70` | read |

Helpers should request **MTU 517** (and 2M PHY where supported).

## track_id
A 32-bit **FNV-1a** hash of the Spotify track URI (for example `spotify:track:4uLU6hMCjMI75M1A2tKUQC`).
Offset basis `0x811C9DC5`, prime `0x01000193`, computed over the URI's UTF-8 bytes.

## State (helper → display), exactly 16 bytes
| Offset | Type | Field |
|---|---|---|
| 0 | u8 | `ver` = 1 |
| 1 | u8 | `flags`: bit0 = playing, bit1 = local (playing on this device rather than remote-controlling another via Spotify Connect) |
| 2 | u8 | `source`: 0 unknown, 1 mac, 2 phone |
| 3 | u8 | reserved = 0 |
| 4 | u32 | `track_id` |
| 8 | u32 | `position_ms` |
| 12 | u32 | `duration_ms` |

Send it on any change (play/pause, seek, track change) and every 5 s while playing. The display timestamps each State on receipt and interpolates progress itself. There is no sender timestamp, because the clocks aren't shared.

## Meta (helper → display), 4–512 bytes
`u32 track_id`, followed by UTF-8 `title 0x1F artist 0x1F album`. Missing fields count as empty. Each field is truncated to 160 bytes on a UTF-8 boundary. Send on track change, **before** State and Art.

## Art (helper → display), chunked
The image is a baseline (non-progressive) JPEG, ideally 160×160, quality about 75. It must be at most 32768 bytes in total.
- Header: `0x01, u32 track_id, u32 total_len`. This starts a new transfer and discards any partial one.
- Data: `0x02, u32 track_id, u32 offset, bytes…`. Offsets must be contiguous from 0. Chunks are about 500 bytes of payload.

The transfer is complete when the contiguous bytes received equal `total_len`. A wrong track_id or a gap aborts the transfer.

## Command (display → helper, notify), 1 byte
`0x01` play/pause · `0x02` next · `0x03` previous.
The display sends it only to the helper whose source is currently shown.

## Info (read)
`u8 protocol_ver = 1, u8 battery_pct` (`0xFF` = unknown).
