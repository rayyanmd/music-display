# Now-Playing Display Firmware (Phase 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** ES3C28P firmware that shows Spotify now-playing info (art, title, artist, album, progress, source badge) received over BLE from up to 3 helper apps, and sends play/pause/next/prev taps back to the active helper.

**Architecture:** All decoding, arbitration and layout math lives in a hardware-free `lib/core` library, unit-tested on the host with `pio test -e native`. Thin device layers (`src/ui.cpp` with LovyanGFX, `src/ble_service.cpp` with NimBLE-Arduino) feed it and draw from it. A Python `bleak` script on the Mac stands in for the helpers until the real helpers exist (later plans).

**Tech Stack:** PlatformIO (pioarduino, Arduino-ESP32 3.x), LovyanGFX 1.2.x, NimBLE-Arduino 2.x, Unity (native tests), Python 3.12 + bleak (test sender).

**Spec:** `docs/superpowers/specs/2026-11-26-now-playing-display-design.md`

**Later plans (not this one):** Mac helper (spec §2, build step 4), Android helper (§3, step 5), power/battery polish plus bonding and the BOOT pairing window (step 6).

## Global Constraints
- Board: ES3C28P. Display 320×240, rotation 1. Pins and display config come from `.agents/skills/es3c28p-display/assets/LGFX_ES3C28P.hpp` (copy it in unchanged).
- Build env: copy `.agents/skills/es3c28p-display/assets/platformio/platformio.ini` (qio_opi, 16MB, CDC on boot), which is already proven to compile.
- Speaker amp off: drive GPIO1 HIGH at boot.
- BLE: the display is the peripheral, with up to 3 simultaneous centrals. Re-advertise after every connect and disconnect.
- Art: baseline JPEG, drawn at 160×160 at (8,24). Reject an art transfer whose total_len is over 32768 bytes.
- Updates appear on screen within 0.5 s of a write.
- Text is UTF-8 and may contain non-ASCII. Render with a LovyanGFX font that covers Latin-1 plus CJK (`fonts::lgfxJapanGothicP_16` for the title, `fonts::lgfxJapanGothicP_12` for the rest).
- Dark theme: background `0x0000`, primary text white, secondary text `0xAD55` (grey), accent `0x1DB954` (Spotify green) converted to RGB565 `0x1DCA`.
- Protocol values are fixed in Task 1 and must be used verbatim by all later plans.

## Review Focus
1. **Non-ASCII and very long titles** (e.g. "Ólafur Arnalds – …", Japanese titles, 200-char names) must render without garbage or overflow, and long text should marquee. Test: `test_meta_utf8_and_truncation` in Task 2.
2. **Malformed or short BLE writes** (truncated State, Meta without separators, Art data before its header, an offset gap, total_len over the cap) must be ignored without crashing or corrupting the current display. Tests: `test_state_too_short`, `test_meta_missing_fields`, `test_art_*` in Task 2.
3. **A helper disconnecting mid-song or mid-art-transfer**: its slot is cleared, the display falls back to another source or the idle screen, and a half-received art buffer is never drawn. Test: `test_disconnect_active_falls_back` in Task 3.
4. **Spotify Connect duplicates** (two sources reporting the same track) must not flip the badge. Test: `test_same_track_prefers_local_and_holds_3s` in Task 3.
5. **Progress past the end or paused** must not overrun the duration, and the bar must freeze when paused. Test: `test_progress_clamps_and_freezes` in Task 3.

---

## File Structure
```
protocol/PROTOCOL.md                 GATT UUIDs + byte layouts (shared with later plans)
firmware/platformio.ini              env:es3c28p (device) + env:native (host tests)
firmware/include/LGFX_ES3C28P.hpp    copied from skill
firmware/lib/core/protocol.h/.cpp    decode State/Meta, ArtAssembler
firmware/lib/core/now_playing.h/.cpp per-source slots, arbitration, progress
firmware/lib/core/layout.h/.cpp      screen rects, hit-testing, time formatting
firmware/src/ui.h/.cpp               LovyanGFX rendering from a NowPlaying snapshot
firmware/src/ble_service.h/.cpp      NimBLE GATT server -> NowPlaying, Command notify
firmware/src/main.cpp                setup/loop, touch -> command, DEMO mode
firmware/test/test_protocol/…        Unity tests (native)
firmware/test/test_now_playing/…
firmware/test/test_layout/…
tools/ble_send.py                    Mac test sender (bleak)
tools/requirements.txt
```

---

### Task 1: Protocol spec + firmware scaffold with native tests

**Files:**
- Create: `protocol/PROTOCOL.md`, `firmware/platformio.ini`, `firmware/include/LGFX_ES3C28P.hpp`, `firmware/src/main.cpp` (stub), `firmware/lib/core/library.json`, `firmware/test/test_smoke/test_main.cpp`

**Interfaces:**
- Produces: the protocol constants below, which every later task and plan uses verbatim.

`protocol/PROTOCOL.md` must contain exactly these values (all multi-byte integers little-endian):

| Name | UUID |
|---|---|
| Service | `6e0a0000-7b5d-4c1e-9f3a-2d8c5b1e4a70` |
| State | `6e0a0001-7b5d-4c1e-9f3a-2d8c5b1e4a70` (write without response) |
| Meta | `6e0a0002-7b5d-4c1e-9f3a-2d8c5b1e4a70` (write) |
| Art | `6e0a0003-7b5d-4c1e-9f3a-2d8c5b1e4a70` (write without response) |
| Command | `6e0a0004-7b5d-4c1e-9f3a-2d8c5b1e4a70` (notify) |
| Info | `6e0a0005-7b5d-4c1e-9f3a-2d8c5b1e4a70` (read) |

- **State** (16 bytes): `u8 ver=1, u8 flags (bit0 playing, bit1 local), u8 source (0 unknown, 1 mac, 2 phone), u8 reserved=0, u32 track_id, u32 position_ms, u32 duration_ms`. *Deviation from the spec, recorded here:* the spec's `timestamp` is dropped. The display stamps receipt time with its own `millis()`, because the helper and display clocks aren't shared and link latency is under 50 ms.
- **Meta**: `u32 track_id` then UTF-8 `title 0x1F artist 0x1F album`. Total ≤ 512 bytes.
- **Art**: header `0x01, u32 track_id, u32 total_len`. Data `0x02, u32 track_id, u32 offset, bytes…`. Complete when contiguous bytes == total_len.
- **Command**: 1 byte: `0x01` play/pause, `0x02` next, `0x03` prev.
- **Info** (read): `u8 protocol_ver=1, u8 battery_pct` (`0xFF` = unknown).
- `track_id`: FNV-1a 32-bit hash of the Spotify track URI (e.g. `spotify:track:…`), computed by helpers.
- Advertised name: `NowPlaying-XXXX`, where XXXX is the last 2 MAC bytes in hex.

`firmware/platformio.ini`: the skill's `[env:es3c28p]` with `lib_deps` plus `h2zero/NimBLE-Arduino@^2.3.0`, and a new env:
```ini
[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17
lib_compat_mode = off
test_filter = test_*
```
`lib/core/library.json`: `{"name":"core","version":"0.1.0","platforms":"*"}`. Core must not include Arduino headers.

- [ ] **Step 1:** Write `protocol/PROTOCOL.md` with the values above.
- [ ] **Step 2:** Create the scaffold. Copy the skill's `platformio.ini`, `include/LGFX_ES3C28P.hpp` and `src/main.cpp` from `.agents/skills/es3c28p-display/assets/platformio/`, then add the native env and the NimBLE dep.
- [ ] **Step 3:** Write `test/test_smoke/test_main.cpp` with one Unity test `test_truth` (`TEST_ASSERT_TRUE(true)`).
- [ ] **Step 4:** Run `cd firmware && pio test -e native`. Expected: `1 Tests 0 Failures`.
- [ ] **Step 5:** Run `pio run -e es3c28p`. Expected: `SUCCESS`.
- [ ] **Step 6:** Commit with `git commit -m "feat: protocol spec and firmware scaffold with native tests"`.

---

### Task 2: Protocol decoding (`lib/core/protocol.*`)

**Files:**
- Create: `firmware/lib/core/protocol.h`, `firmware/lib/core/protocol.cpp`, `firmware/test/test_protocol/test_main.cpp`

**Interfaces:**
- Produces:
```cpp
namespace proto {
enum class Source : uint8_t { Unknown = 0, Mac = 1, Phone = 2 };
enum class Cmd : uint8_t { PlayPause = 1, Next = 2, Prev = 3 };
struct State { uint32_t trackId; uint32_t positionMs; uint32_t durationMs; bool playing; bool local; Source source; };
struct Meta  { uint32_t trackId; std::string title, artist, album; };
constexpr size_t kMaxArt = 32768, kMaxField = 160;   // fields truncated to kMaxField bytes on a UTF-8 boundary
bool decodeState(const uint8_t* d, size_t n, State& out);   // false: n!=16 or ver!=1
bool decodeMeta(const uint8_t* d, size_t n, Meta& out);     // false: n<4 or n>512; missing fields -> ""
class ArtAssembler {
 public:
  enum class Result { Ignored, InProgress, Complete, Error };
  Result feed(const uint8_t* d, size_t n);   // header resets; data must match trackId and offset==received
  uint32_t trackId() const; const std::vector<uint8_t>& data() const; void reset();
};
}
```

- [ ] **Step 1: Write the failing tests** in `test/test_protocol/test_main.cpp`:
  - `test_state_roundtrip`: bytes `{1, 0b11, 1, 0, 0x78,0x56,0x34,0x12, 0xE8,0x03,0,0, 0x60,0xEA,0,0}` decode to trackId `0x12345678`, positionMs 1000, durationMs 60000, playing and local true, source Mac.
  - `test_state_too_short`: a 15-byte buffer returns false. A buffer with ver=2 returns false.
  - `test_meta_three_fields`: `{id LE} "Song\x1FArtist\x1FAlbum"` gives the three fields.
  - `test_meta_missing_fields`: `"Song"` only gives title "Song" with artist and album "".
  - `test_meta_utf8_and_truncation`: a title of 200× "é" (2 bytes each) is truncated to ≤160 bytes. `std::string` length is even and ends on a complete code point. "東京" survives unchanged.
  - `test_art_complete`: a header (id 7, len 1200) followed by 3 data chunks at offsets 0/500/1000 returns Complete on the last, and `data()` equals the input.
  - `test_art_data_before_header_ignored`: returns Ignored.
  - `test_art_offset_gap_errors`: an offset-500 chunk first returns Error, and the assembler resets.
  - `test_art_too_large_errors`: a header with len 40000 returns Error.
  - `test_art_new_header_restarts`: a header for id 8 mid-transfer of id 7 means `trackId()==8` and the received count resets.
- [ ] **Step 2:** Run `pio test -e native -f test_protocol`. Expected: compile failure (undefined `proto::`).
- [ ] **Step 3:** Implement `protocol.h/.cpp`. UTF-8 truncation backs up while `(byte & 0xC0) == 0x80`.
- [ ] **Step 4:** Run `pio test -e native -f test_protocol`. Expected: all 10 pass.
- [ ] **Step 5:** Commit with `git commit -m "feat(core): protocol decoding and art reassembly"`.

---

### Task 3: Now-playing model and arbitration (`lib/core/now_playing.*`)

**Files:**
- Create: `firmware/lib/core/now_playing.h`, `firmware/lib/core/now_playing.cpp`, `firmware/test/test_now_playing/test_main.cpp`

**Interfaces:**
- Consumes: `proto::State`, `proto::Meta`, `proto::ArtAssembler` (Task 2).
- Produces:
```cpp
struct Track { uint32_t id = 0; std::string title, artist, album; uint32_t durationMs = 0; };
struct Snapshot {                      // what the UI draws
  bool hasActive = false; proto::Source source = proto::Source::Unknown;
  Track track; bool playing = false; uint32_t positionMs = 0;   // interpolated
  const std::vector<uint8_t>* art = nullptr;   // complete JPEG for track.id, else nullptr
  uint32_t artVersion = 0;                      // bumps when new art completes
  uint8_t connected = 0;
};
class NowPlaying {
 public:
  static constexpr int kSlots = 3;
  void onConnect(uint16_t conn);  void onDisconnect(uint16_t conn);
  void onState(uint16_t conn, const proto::State& s, uint32_t nowMs);
  void onMeta(uint16_t conn, const proto::Meta& m);
  void onArt(uint16_t conn, const uint8_t* d, size_t n);
  Snapshot snapshot(uint32_t nowMs) const;
  int activeConn() const;   // conn handle that commands go to, -1 if none
};
```
Arbitration rules (from the spec), in order, evaluated on every `onState` and `onDisconnect`:
1. A slot whose State says `playing` becomes active, unless rule 2 applies.
2. If the candidate's `trackId` equals the active slot's `trackId`: switch only if the active slot is non-local and the candidate is local, *and* at least 3000 ms have passed since the last switch.
3. If no slot is playing, keep the current active slot (shown paused).
4. If the active slot disconnects, pick the most recently updated remaining slot, or none.

Progress: `positionMs + (playing ? nowMs - receivedAtMs : 0)`, clamped to `durationMs`.

- [ ] **Step 1: Write the failing tests:**
  - `test_first_playing_source_becomes_active`
  - `test_newer_playing_source_takes_over`: Mac playing track A, then Phone playing track B, so active is Phone.
  - `test_paused_keeps_last_active`: Mac pauses, so it stays active with `playing=false`.
  - `test_same_track_prefers_local_and_holds_3s`: Mac (non-local) is active on track A at t=0. Phone (local) reports track A at t=1000, so it's still Mac. At t=3500, Phone reports again, so it switches to Phone. At t=4000, Mac (non-local) reports A, so it stays Phone.
  - `test_disconnect_active_falls_back`: Mac and Phone are connected with Mac active, and Phone has a partial art transfer. Mac disconnects, so Phone becomes active and `snapshot.art == nullptr`. Phone disconnects, so `hasActive == false` and `connected == 0`.
  - `test_progress_clamps_and_freezes`: position 59000, duration 60000, playing; snapshot at +5000 ms gives 60000. Paused at 30000; snapshot at +5000 gives 30000.
  - `test_meta_and_art_only_shown_for_matching_track`: art completed for id 7 while the state says id 8 gives `art == nullptr`. After the state switches to 7, the art is shown and `artVersion` has changed.
- [ ] **Step 2:** Run `pio test -e native -f test_now_playing`. Expected: compile failure.
- [ ] **Step 3:** Implement. Each slot holds `conn`, `State`, `Meta`, its own `ArtAssembler`, the last complete art `{id, bytes}`, `receivedAtMs` and `updatedAtMs`. Unused slots have `conn = 0xFFFF`.
- [ ] **Step 4:** Run `pio test -e native`. Expected: all suites pass.
- [ ] **Step 5:** Commit with `git commit -m "feat(core): now-playing model with source arbitration"`.

---

### Task 4: Layout, hit-testing, and UI rendering with demo data

**Files:**
- Create: `firmware/lib/core/layout.h`, `firmware/lib/core/layout.cpp`, `firmware/test/test_layout/test_main.cpp`, `firmware/src/ui.h`, `firmware/src/ui.cpp`, `firmware/src/demo_art.h`
- Modify: `firmware/src/main.cpp`

**Interfaces:**
- Consumes: `Snapshot` (Task 3), `LGFX` (from `LGFX_ES3C28P.hpp`).
- Produces:
```cpp
// layout.h
struct Rect { int16_t x, y, w, h; bool contains(int x, int y) const; };
namespace layout {
constexpr Rect kArt{8, 24, 160, 160};
constexpr Rect kTitle{178, 28, 134, 20}, kArtist{178, 52, 134, 16}, kAlbum{178, 70, 134, 16};
constexpr Rect kProgress{178, 140, 134, 6};
constexpr Rect kTimes{178, 150, 134, 14};
constexpr Rect kPrev{178, 176, 40, 48}, kPlay{225, 176, 40, 48}, kNext{272, 176, 40, 48};
constexpr Rect kStatus{0, 0, 320, 20};
enum class Button { None, Prev, PlayPause, Next };
Button hitTest(int x, int y);           // touch slop: each button rect expanded by 6 px
std::string formatTime(uint32_t ms);    // "m:ss", or "h:mm:ss" at 1 h or more
int progressWidth(uint32_t pos, uint32_t dur, int fullW);   // 0 if dur==0
}
// ui.h
class Ui { public: void begin(LGFX& tft); void render(const Snapshot& s, uint32_t nowMs); void flashButton(layout::Button b); };
```
- [ ] **Step 1: Write the failing tests:**
  - `test_hit_buttons`: the centers of kPrev, kPlay and kNext map to their buttons. (5,5) gives None. (kNext.x−3, kNext.y+10) gives Next (slop).
  - `test_format_time`: 0 → "0:00", 65000 → "1:05", 3725000 → "1:02:05".
  - `test_progress_width`: (30000, 60000, 134) gives 67, (0, 0, 134) gives 0, and (70000, 60000, 134) gives 134.
- [ ] **Step 2:** Run `pio test -e native -f test_layout`. Expected: compile failure.
- [ ] **Step 3:** Implement `layout.cpp`.
- [ ] **Step 4:** Run `pio test -e native`. Expected: all pass.
- [ ] **Step 5:** Implement `Ui`. A 320×240 `LGFX_Sprite` in PSRAM is drawn once per change and pushed. Art is decoded with `sprite.drawJpg(ptr, len, kArt.x, kArt.y, 160, 160, 0, 0, scale)`, where scale fits a non-160 image into 160. It is re-decoded only when `artVersion` changes, using a cached 160×160 art sprite. With no art, draw a placeholder (a dark grey rounded square with a ♪). Title and artist wider than their rect scroll as a marquee at 30 px/s with a 1 s pause at the ends. The status row shows the badge text "● Mac" / "● Phone" / "● Device" in green, plus `connected` link dots. Idle (`!hasActive`): a centered "Nothing playing" and the "Waiting for Mac or phone" subtext. Render at most every 33 ms.
- [ ] **Step 6:** In `main.cpp`, add the `-DDEMO=1` build path, which feeds a fake Snapshot (title "Ólafur Arnalds — Saman (Long Title For Marquee Test)", artist "東京 Test Artist", duration 3:45, playing) plus a ~8 KB baseline JPEG in `demo_art.h`, generated by `sips`/`python -I -c` from any image and stored as a `const uint8_t[]`. Touch calls `hitTest`, prints `Serial.printf("tap %d\n", ...)` and calls `flashButton`. Add `[env:demo]`, which extends `es3c28p` with `build_flags = ${env:es3c28p.build_flags} -DDEMO=1`.
- [ ] **Step 7:** Run `pio run -e demo -t upload && pio device monitor`. Expected on the device:
  - the layout matches the spec sketch;
  - the art has correct colors;
  - the title and artist marquee, and the Japanese text renders;
  - the progress bar advances;
  - tapping ⏮/⏯/⏭ prints `tap 1/2/3` and flashes the button.
- [ ] **Step 8:** Commit with `git commit -m "feat: now-playing UI with demo mode"`.

---

### Task 5: BLE GATT server + Mac test sender

**Files:**
- Create: `firmware/src/ble_service.h`, `firmware/src/ble_service.cpp`, `tools/ble_send.py`, `tools/requirements.txt`
- Modify: `firmware/src/main.cpp`

**Interfaces:**
- Consumes: the protocol constants (Task 1), `proto::decode*` (Task 2), `NowPlaying` (Task 3), `Ui` and `layout::hitTest` (Task 4).
- Produces:
```cpp
class BleService {
 public:
  void begin(NowPlaying& model, std::mutex& lock);   // init NimBLE, GATT, advertising
  void sendCommand(int conn, proto::Cmd c);           // notify Command to one conn only
  void setBatteryPct(uint8_t pct);                    // backs the Info read (0xFF until power plan)
};
```
`tools/ble_send.py` CLI (bleak ≥ 0.22):
`python tools/ble_send.py --title T --artist A --album B --uri spotify:track:X --duration 225000 --position 30000 [--paused] [--source mac|phone] [--not-local] [--art path.jpg] [--listen 30]`. It scans for the service UUID, connects, and writes Meta, then the Art header and data chunks (500 B), then State. It sends State every 5 s while `--listen` runs, and prints each Command notification received as `CMD play_pause|next|prev`. It resizes `--art` to 160×160 baseline JPEG quality 75 with Pillow.

- [ ] **Step 1:** Implement `BleService`. Device name per Task 1. `NimBLEDevice::setMTU(517)`. Server callbacks: `onConnect` calls `model.onConnect(conn)` and restarts advertising; `onDisconnect` calls `model.onDisconnect`. Characteristic `onWrite(chr, connInfo)` locks the mutex, decodes, and dispatches to the model using `connInfo.getConnHandle()`. Invalid payloads are dropped silently (log at debug level).
- [ ] **Step 2:** Wire it into `main.cpp` (non-DEMO build). Each loop: lock, take `snapshot(millis())`, unlock, then `ui.render`. On a tap: `int c = model.activeConn(); if (c >= 0) ble.sendCommand(c, cmd); ui.flashButton(b);`. Keep GPIO1 HIGH.
- [ ] **Step 3:** Run `pio test -e native && pio run -e es3c28p -t upload`. Expected: tests pass, the upload succeeds, and the device shows "Nothing playing".
- [ ] **Step 4:** Write `tools/ble_send.py` and `requirements.txt` (`bleak>=0.22`, `Pillow>=10`). Run `uv venv tools/.venv && uv pip install --python tools/.venv/bin/python -r tools/requirements.txt`. On first run, macOS asks the terminal app for Bluetooth permission; allow it.
- [ ] **Step 5:** Run `tools/.venv/bin/python tools/ble_send.py --title "Test Song" --artist "Artist" --album "Album" --uri spotify:track:abc --duration 225000 --position 30000 --source mac --art <any.jpg> --listen 30`. Expected:
  - the display shows the track, art and "● Mac" within 0.5 s, and the progress bar advances;
  - tapping ⏭ on the display prints `CMD next` in the terminal.
- [ ] **Step 6:** Two-source check. While run A is listening (`--source mac --not-local`, uri X), start run B (`--source phone`, same uri X). The badge holds on Mac for 3 s, then shows Phone. ⏯ prints the command in B's terminal only. Ctrl-C on B makes the display fall back to Mac.
- [ ] **Step 7:** Disconnect check. Ctrl-C the sender mid-art (use a large `--art`). The display returns to idle or the other source, shows no partial art, and doesn't crash (monitor shows no panic).
- [ ] **Step 8:** Commit with `git commit -m "feat: BLE GATT server and Mac test sender"`.
