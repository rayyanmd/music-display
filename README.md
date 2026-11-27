# Music Display

A small wireless "now playing" screen for Spotify. An ESP32-S3 board with a 2.8" touch display shows what's playing on your Mac (and, later, your Android phone):
- the title, artist, album and album art
- a live progress bar
- synced lyrics
- touch buttons for ⏮ ⏯ ⏭

Everything travels over **Bluetooth LE**, so the display needs no Wi-Fi and no Spotify login. A small helper app on each device reads Spotify and sends the data to the display.

```
 Mac menu-bar helper (Swift) ──BLE──┐
                                    ├──► ESP32-S3 display (BLE peripheral, up to 3 helpers)
 Android helper (planned)    ──BLE──┘      touch ⏮ ⏯ ⏭ → back to the helper that's shown → Spotify
```

```
┌──────────────────────────────────────────┐        ┌──────────────────────────────────────────┐
│ ● Mac                              ● ●   │        │ ● Mac                              ● ●   │
│ ┌────────────┐  Song Title               │  tap   │   previous line                          │
│ │            │  Artist                   │  art   │   CURRENT LINE (wraps to two rows)       │
│ │  album art │  Album                    │ ─────► │   next line                              │
│ │  160×160   │  ━━━━━━━━━──────          │ ◄───── │   next line                              │
│ │            │  1:42           3:55      │  tap   │  ━━━━━━━━━━━━━━━──────────────────────── │
│ └────────────┘     ⏮    ⏯    ⏭           │ lyrics │  Song — Artist         ⏮    ⏯    ⏭       │
└──────────────────────────────────────────┘        └──────────────────────────────────────────┘
```

## Features
- **Instant updates.** Changes show in under half a second, with no cloud round trip.
- **Album art**, cropped and re-encoded by the helper to a 160×160 baseline JPEG.
- **Touch controls** (previous, play/pause, next) go to whichever device is currently shown.
- **Several sources.** The display accepts up to 3 helpers at once and shows the one that most recently started playing, with a "● Mac" or "● Phone" badge. It avoids flicker when one device remote-controls the other through Spotify Connect.
- **Lyrics** from [LRCLIB](https://lrclib.net), free and with no key. Tap the art to open the lyrics screen. Synced lyrics highlight line by line, and untimed lyrics scroll with the song. The lyrics catch up within about 2 s after a seek.
- **Works offline.** Lyrics need internet on the Mac. Everything else works with no network at all.

## Hardware
[LCDwiki 2.8" ESP32-S3 Display](https://lcdwiki.com/2.8inch_ESP32-S3_Display) **ES3C28P** (the touch version):
- ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM)
- ILI9341V 320×240 IPS panel over SPI
- FT6336 capacitive touch
- native USB, which shows up on macOS as `/dev/cu.usbmodem*`

The touch panel is mounted at 180° to the LCD, so the firmware handles that (`offset_rotation = 2`). Board notes, the pin map and the gotchas live in the bundled skill at `.agents/skills/es3c28p-display/`.

## Repository layout
| Path | What it is |
|---|---|
| `protocol/PROTOCOL.md` | The BLE GATT protocol (v1.1) that all parts implement. **The binding spec.** |
| `firmware/` | Display firmware: PlatformIO, Arduino core 3.3, LovyanGFX, NimBLE-Arduino. `lib/core` is hardware-free and unit-tested on the host. |
| `mac-helper/` | macOS menu-bar app: Swift 6, SwiftPM, no Xcode needed. `HelperCore` is pure and unit-tested. |
| `tools/ble_send.py` | A stand-in helper for testing the display from Python (bleak): meta, art, state and lyrics. |
| `docs/superpowers/` | Design specs and implementation plans. |

## Getting started

### 1. Flash the display
Requirements: [PlatformIO](https://platformio.org) (`pipx install platformio`).

```sh
cd firmware
pio run -e es3c28p -t upload --upload-port /dev/cu.usbmodem101   # adjust the port
pio device monitor -b 115200                                     # optional logs
```

The display then advertises as `NowPlaying-XXXX` and shows "Waiting for Mac or phone".

For a UI demo with a fake track and no Bluetooth, run `pio run -e demo -t upload`.

### 2. Install the Mac helper
Requirements: macOS 14+, Swift 6 (the Command Line Tools are enough), and the Spotify desktop app.

```sh
cd mac-helper
scripts/make-cert.sh   # once: creates a local "Music Display Dev" signing identity
scripts/bundle.sh      # builds, signs, installs to ~/Applications/MusicDisplay.app and opens it
```

On first launch, allow **Bluetooth** and **Automation → Spotify** when macOS asks. The stable self-signed certificate keeps those permissions across rebuilds.

A music-note icon appears in the menu bar. Its menu shows the connection state and the current track, and has a **Launch at login** toggle.

### 3. Play something
Start Spotify. The display picks up the track within a moment. Tap ⏮ ⏯ ⏭ to control Spotify, and tap the album art to show the lyrics.

## Testing
```sh
cd firmware && pio test -e native        # Unity tests for lib/core, run under ASan/UBSan
mac-helper/scripts/test.sh               # swift-testing (wraps `swift test` with the CLT Testing framework paths)
```

To exercise the display without the Mac app, quit Music Display first. The Mac shares one Bluetooth link to the display.

```sh
python3 -m venv tools/.venv && tools/.venv/bin/pip install -r tools/requirements.txt
tools/.venv/bin/python tools/ble_send.py --title "Test Song" --artist Artist --art cover.jpg \
    --lrc song.lrc --listen 60          # also: --plain FILE, --lyrics-kind none|instrumental, --source phone
```

## Privacy
- Track data goes only from your devices to the display, over Bluetooth.
- For lyrics, the Mac helper sends each new song's **title, artist, album and duration** to `lrclib.net`. That is the only outside service it contacts besides Spotify's own album-art host.
- Ads and podcasts are never looked up.

## Status and roadmap
- [x] Display firmware: UI, touch, BLE, multi-source arbitration
- [x] Mac helper
- [x] Lyrics (protocol v1.1)
- [ ] Android helper (Kotlin, NotificationListener and CompanionDeviceManager)
- [ ] Power polish: backlight dimming and sleep, battery level, BLE bonding with a pairing window on the BOOT button

## Limitations
- Spotify only. Spotify's own lyrics aren't available to third-party apps, so lyrics come from LRCLIB, and some songs won't be there.
- The display font (efontJA) covers Latin, accented Latin and Japanese. It has no emoji, and some Chinese and Korean characters and right-to-left scripts may be missing.
- Spotify's Mac scripting interface doesn't say whether it is remote-controlling another device, so the Mac always reports itself as the device playing.
